#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "protocol/AuthUtils.h"
#include "protocol/ChatHandler.h"
#include "protocol/PacketTypes.h"
#include "world/EntityManager.h"
#include "world/EventQueue.h"
#include "world/WorldTicker.h"

using namespace mcpe;
using namespace mcpe::protocol;
using namespace mcpe::world;

TEST(Sprint3Test, RelMoveDeltaMathAndAngleQuantization) {
  // 1. 验证 1/4096 方块定点数转换精度
  constexpr double dx = 1.5;
  const auto encoded_dx = static_cast<int16_t>(std::round(dx * 4096.0));
  EXPECT_EQ(encoded_dx, 6144);

  constexpr double negative_dy = -0.25;
  const auto encoded_dy = static_cast<int16_t>(std::round(negative_dy * 4096.0));
  EXPECT_EQ(encoded_dy, -1024);

  // 2. 视角 Angle 字节量化 (0~255)
  Rotation rot_east{90.0f, 0.0f};
  EXPECT_EQ(rot_east.toByteYaw(), 64);
  EXPECT_EQ(rot_east.toBytePitch(), 0);

  Rotation rot_south{180.0f, 45.0f};
  EXPECT_EQ(rot_south.toByteYaw(), 128);
  EXPECT_EQ(rot_south.toBytePitch(), 32);

  Rotation rot_north{0.0f, -90.0f};
  EXPECT_EQ(rot_north.toByteYaw(), 0);
  EXPECT_EQ(rot_north.toBytePitch(), static_cast<uint8_t>((-90.0f / 360.0f) * 256.0f));
}

TEST(Sprint3Test, BinaryUuidGenerationAndParsing) {
  // Steve 官方离线 UUID
  const auto steve_bytes = AuthUtils::generateOfflineUuidBytes("Steve");
  const std::string steve_str = AuthUtils::generateOfflineUuid("Steve");
  EXPECT_EQ(steve_str, "5627dd98-e6be-3c21-b8a8-e92344183641");

  // 验证字节解析回环
  const auto parsed = AuthUtils::parseUuidStringToBytes(steve_str);
  EXPECT_EQ(steve_bytes, parsed);
}

TEST(Sprint3Test, EntityManagerSpawnAndVisibilitySequence) {
  std::vector<std::pair<uint64_t, int32_t>> sent_packets;

  auto sender = [&](uint64_t sid, ByteBuffer pkt) {
    auto len_res = pkt.readVarInt();
    auto id_res = pkt.readVarInt();
    if (len_res && id_res) {
      sent_packets.emplace_back(sid, *id_res);
    }
  };

  EntityManager mgr(sender, nullptr, nullptr);

  const auto steve_uuid = AuthUtils::generateOfflineUuidBytes("Steve");
  mgr.addPlayer(1, 101, "Steve", steve_uuid);
  EXPECT_EQ(mgr.playerCount(), 1);

  // 第一个玩家加入：发送自身的 0x2E
  ASSERT_EQ(sent_packets.size(), 1);
  EXPECT_EQ(sent_packets[0].first, 1);
  EXPECT_EQ(sent_packets[0].second, ClientboundPacket::PlayerListItem);

  sent_packets.clear();

  // 第二个玩家 Alex 加入
  const auto alex_uuid = AuthUtils::generateOfflineUuidBytes("Alex");
  mgr.addPlayer(2, 102, "Alex", alex_uuid);
  EXPECT_EQ(mgr.playerCount(), 2);

  // 期望触发双向可见性时序：
  // 1. 给 Steve 发 Alex 的 0x2E 与 0x05
  // 2. 给 Alex 发 Steve 的 0x2E 与 0x05
  // 3. 给 Alex 发自身的 0x2E
  EXPECT_EQ(sent_packets.size(), 5);
}

TEST(Sprint3Test, DeltaMovementVsTeleportThreshold) {
  std::vector<int32_t> broadcast_packet_ids;

  auto broadcast_except = [&](uint64_t /*except_sid*/, ByteBuffer pkt) {
    auto len_res = pkt.readVarInt();
    auto id_res = pkt.readVarInt();
    if (len_res && id_res) {
      broadcast_packet_ids.push_back(*id_res);
    }
  };

  EntityManager mgr(nullptr, broadcast_except, nullptr);
  const auto uuid = AuthUtils::generateOfflineUuidBytes("Steve");
  mgr.addPlayer(1, 101, "Steve", uuid, Vector3d{0.0, 64.0, 0.0}, Rotation{0.0f, 0.0f});

  // 1. 小位移 (1.0 格)：触发 0x26 Relative Move
  mgr.updatePlayerPosition(1, Vector3d{1.0, 64.0, 0.0}, std::nullopt, true);
  mgr.updateAndBroadcast();

  ASSERT_EQ(broadcast_packet_ids.size(), 1);
  EXPECT_EQ(broadcast_packet_ids[0], ClientboundPacket::EntityRelativeMove);
  broadcast_packet_ids.clear();

  // 2. 仅转头：触发 0x28 Entity Look
  mgr.updatePlayerPosition(1, std::nullopt, Rotation{90.0f, 0.0f}, true);
  mgr.updateAndBroadcast();

  ASSERT_EQ(broadcast_packet_ids.size(), 1);
  EXPECT_EQ(broadcast_packet_ids[0], ClientboundPacket::EntityLook);
  broadcast_packet_ids.clear();

  // 3. 同时位移与转头：触发 0x27 Look And Relative Move
  mgr.updatePlayerPosition(1, Vector3d{2.0, 64.0, 0.0}, Rotation{180.0f, 0.0f}, true);
  mgr.updateAndBroadcast();

  ASSERT_EQ(broadcast_packet_ids.size(), 1);
  EXPECT_EQ(broadcast_packet_ids[0], ClientboundPacket::EntityLookAndRelativeMove);
  broadcast_packet_ids.clear();

  // 4. 超过 8.0 格大位移 (瞬移 10.0 格)：降级下发 0x4C Entity Teleport
  mgr.updatePlayerPosition(1, Vector3d{12.0, 64.0, 0.0}, Rotation{180.0f, 0.0f}, true);
  mgr.updateAndBroadcast();

  ASSERT_EQ(broadcast_packet_ids.size(), 1);
  EXPECT_EQ(broadcast_packet_ids[0], ClientboundPacket::EntityTeleport);
}

TEST(Sprint3Test, ChatJsonEscapingAndCommandRouting) {
  // 1. JSON 特殊字符转义
  std::string raw = R"(Hello "World" \ Backslash)";
  std::string escaped = ChatHandler::escapeJson(raw);
  EXPECT_EQ(escaped, R"(Hello \"World\" \\ Backslash)");

  // 2. 指令拦截与分流
  std::vector<std::string> sys_messages;
  std::vector<std::string> public_messages;

  ChatHandler chat(
      [&](uint64_t /*sid*/, ByteBuffer pkt) {
        auto len = pkt.readVarInt();
        ASSERT_TRUE(len.has_value());
        auto id = pkt.readVarInt();
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(*id, ClientboundPacket::ChatMessage);
        auto json = pkt.readString();
        ASSERT_TRUE(json.has_value());
        sys_messages.push_back(*json);
      },
      [&](ByteBuffer pkt) {
        auto len = pkt.readVarInt();
        ASSERT_TRUE(len.has_value());
        auto id = pkt.readVarInt();
        ASSERT_TRUE(id.has_value());
        EXPECT_EQ(*id, ClientboundPacket::ChatMessage);
        auto json = pkt.readString();
        ASSERT_TRUE(json.has_value());
        public_messages.push_back(*json);
      }
  );

  // 公屏聊天
  chat.handleMessage(1, "Steve", "Hello everyone!");
  EXPECT_EQ(public_messages.size(), 1);
  EXPECT_EQ(sys_messages.size(), 0);
  EXPECT_NE(public_messages[0].find(R"(<Steve> Hello everyone!)"), std::string::npos);

  // 快捷指令：绝不流入公屏
  chat.handleMessage(1, "Steve", "/maps");
  EXPECT_EQ(public_messages.size(), 1);
  EXPECT_EQ(sys_messages.size(), 1);
  EXPECT_NE(sys_messages[0].find("地图列表"), std::string::npos);

  chat.handleMessage(1, "Steve", "/lobby");
  EXPECT_EQ(sys_messages.size(), 2);
  EXPECT_NE(sys_messages[1].find("已经在主大厅"), std::string::npos);

  chat.handleMessage(1, "Steve", "/unknown");
  EXPECT_EQ(sys_messages.size(), 3);
  EXPECT_NE(sys_messages[2].find("未知指令"), std::string::npos);
}

TEST(Sprint3Test, WorldTickerSingleWriterStepPipeline) {
  EventQueue queue;
  EntityManager entity_mgr;
  std::string captured_chat;

  WorldTicker ticker(queue, entity_mgr, [&](uint64_t, std::string_view, const std::string& msg) {
    captured_chat = msg;
  });

  // 1. 模拟网络线程异步投递登入事件
  WorldEvent join_ev;
  join_ev.session_id = 1;
  join_ev.entity_id = 101;
  PlayerJoinEvent join;
  join.setName("Steve");
  join.uuid = AuthUtils::generateOfflineUuidBytes("Steve");
  join_ev.payload = std::move(join);
  queue.push(std::move(join_ev));

  // 2. 模拟网络线程异步投递聊天事件
  WorldEvent chat_ev;
  chat_ev.session_id = 1;
  chat_ev.entity_id = 101;
  chat_ev.payload = PlayerChatEvent{.message = "Testing Tick Pipeline"};
  queue.push(std::move(chat_ev));

  // 3. 执行单 Tick 物理驱动
  ticker.tick();

  EXPECT_TRUE(entity_mgr.hasPlayer(1));
  EXPECT_EQ(entity_mgr.getPlayerName(1), "Steve");
  EXPECT_EQ(captured_chat, "Testing Tick Pipeline");

  // 4. 模拟玩家退服
  WorldEvent leave_ev;
  leave_ev.session_id = 1;
  leave_ev.entity_id = 101;
  leave_ev.payload = PlayerLeaveEvent{};
  queue.push(std::move(leave_ev));

  ticker.tick();
  EXPECT_FALSE(entity_mgr.hasPlayer(1));
}
