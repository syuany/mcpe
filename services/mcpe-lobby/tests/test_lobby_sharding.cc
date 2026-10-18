/**
 * @file test_lobby_sharding.cc
 * @brief 大厅满员自动裂变分线 (lobby-1..N)、只读地图内存共享与 /setmaxplayers 持久化单元测试
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "lobby/LobbyManager.h"
#include "protocol/AuthUtils.h"
#include "protocol/ChatHandler.h"
#include "protocol/GameTypes.h"
#include "world/World.h"

using namespace mcpe;
using namespace mcpe::lobby;
using namespace mcpe::protocol;
using namespace mcpe::world;

TEST(LobbyShardingTest, AutoSpawnsNewLobbyShardWhenFullAndSharesWorldMemory) {
  auto shared_lobby_world = std::make_shared<World>();
  shared_lobby_world->setSpawnPoint(Vector3d{48.5, 34.0, 48.5});
  shared_lobby_world->getOrCreateChunk(ChunkPos{3, 3}).setBlock(BlockPos{48, 33, 48}, BlockStates::kStone);

  // 设置单个大厅分线最多容纳 2 人，最多允许裂变 3 个分线，关闭后台线程使用单步确定性驱动
  LobbyManager mgr(shared_lobby_world, /*lobby_max_players=*/2, /*max_lobby_instances=*/3, /*auto_start=*/false);

  // 初始仅存在常驻分线 lobby-1
  EXPECT_EQ(mgr.lobbyInstanceCount(), 1u);

  // 玩家 1 与 玩家 2 进入：分配至 lobby-1
  auto l1 = mgr.acquireLobbyForPlayer(1001);
  auto l2 = mgr.acquireLobbyForPlayer(1002);
  ASSERT_NE(l1, nullptr);
  ASSERT_NE(l2, nullptr);
  EXPECT_EQ(l1->instanceId(), "lobby-1");
  EXPECT_EQ(l2->instanceId(), "lobby-1");
  EXPECT_EQ(l1->playerCount(), 2u);
  EXPECT_TRUE(l1->isFull());

  // 玩家 3 与 玩家 4 进入：lobby-1 已满 (2/2)，自动裂变创建 lobby-2！
  auto l3 = mgr.acquireLobbyForPlayer(1003);
  auto l4 = mgr.acquireLobbyForPlayer(1004);
  ASSERT_NE(l3, nullptr);
  ASSERT_NE(l4, nullptr);
  EXPECT_EQ(mgr.lobbyInstanceCount(), 2u);
  EXPECT_EQ(l3->instanceId(), "lobby-2");
  EXPECT_EQ(l4->instanceId(), "lobby-2");
  EXPECT_EQ(l3->playerCount(), 2u);
  EXPECT_TRUE(l3->isFull());

  // 验证 lobby-1 与 lobby-2 共享完全相同的底层只读 World 内存地址（零区块深拷贝）
  EXPECT_EQ(&l1->world(), &l3->world());
  EXPECT_EQ(l1->worldPtr().get(), shared_lobby_world.get());
  EXPECT_EQ(l3->worldPtr().get(), shared_lobby_world.get());

  // 当玩家 1 从 lobby-1 下线后，新玩家 5 再次请求时应优先回填进未满的 lobby-1
  mgr.onPlayerDisconnected(1001, 1);
  EXPECT_EQ(l1->playerCount(), 1u);
  EXPECT_FALSE(l1->isFull());

  auto l5 = mgr.acquireLobbyForPlayer(1005);
  ASSERT_NE(l5, nullptr);
  EXPECT_EQ(l5->instanceId(), "lobby-1");
  EXPECT_EQ(mgr.lobbyInstanceCount(), 2u);
}

TEST(LobbyShardingTest, InstanceIsolationForChatAndSetMaxPlayersPersistence) {
  const std::filesystem::path tmp_settings = "/tmp/test_sharding_settings.ini";
  const int32_t orig_max = ServerConfig::defaultInstance().max_players;
  const int32_t orig_lobby_max = ServerConfig::defaultInstance().lobby_max_players;

  auto world = std::make_shared<World>();
  auto lobby_mgr = std::make_shared<LobbyManager>(world, 2, 5, false);

  std::vector<std::pair<uint64_t, std::string>> received_packets;
  lobby_mgr->setUnicastCallback([&](uint64_t sid, ByteBuffer pkt) {
    auto len = pkt.readVarInt();
    auto pid = pkt.readVarInt();
    if (len && pid && *pid == ClientboundPacket::ChatMessage) {
      auto json = pkt.readString();
      if (json) {
        received_packets.emplace_back(sid, *json);
      }
    }
  });

  // 玩家 1, 2 在 lobby-1；玩家 3 在 lobby-2
  auto lobby1 = lobby_mgr->acquireLobbyForPlayer(1);
  (void)lobby_mgr->acquireLobbyForPlayer(2);
  auto lobby2 = lobby_mgr->acquireLobbyForPlayer(3);

  (void)lobby_mgr->transferPlayer(1, 101, "Alice", AuthUtils::generateOfflineUuidBytes("Alice"), lobby1);
  (void)lobby_mgr->transferPlayer(2, 102, "Bob", AuthUtils::generateOfflineUuidBytes("Bob"), lobby1);
  (void)lobby_mgr->transferPlayer(3, 103, "Charlie", AuthUtils::generateOfflineUuidBytes("Charlie"), lobby2);

  lobby1->stepTick();
  lobby2->stepTick();
  received_packets.clear();

  ChatHandler chat;
  chat.setLobbyManager(lobby_mgr);
  chat.setSettingsPath(tmp_settings);
  chat.setSenders(
      [&](uint64_t sid, ByteBuffer pkt) {
        auto len = pkt.readVarInt();
        auto pid = pkt.readVarInt();
        if (len && pid && *pid == ClientboundPacket::ChatMessage) {
          auto json = pkt.readString();
          if (json) {
            received_packets.emplace_back(sid, *json);
          }
        }
      },
      nullptr
  );

  // 1. Alice (SID 1) 在 lobby-1 发言：只有 lobby-1 的 Alice(1) 和 Bob(2) 收到，lobby-2 的 Charlie(3) 不会收到！
  chat.handleMessage(1, "Alice", "Hello Lobby 1!");
  EXPECT_EQ(received_packets.size(), 2u);
  for (const auto& [sid, msg] : received_packets) {
    EXPECT_TRUE(sid == 1 || sid == 2);
    EXPECT_NE(msg.find("<Alice> Hello Lobby 1!"), std::string::npos);
  }
  received_packets.clear();

  // 2. 执行 /setmaxplayers 800 60 更新配额并持久化至 settings.ini
  chat.handleMessage(1, "Alice", "/setmaxplayers 800 60");
  EXPECT_EQ(ServerConfig::defaultInstance().max_players, 800);
  EXPECT_EQ(ServerConfig::defaultInstance().lobby_max_players, 60);
  EXPECT_EQ(lobby_mgr->lobbyMaxPlayers(), 60u);

  ServerConfig verify_cfg;
  ASSERT_TRUE(verify_cfg.loadFromIni(tmp_settings));
  EXPECT_EQ(verify_cfg.max_players, 800);
  EXPECT_EQ(verify_cfg.lobby_max_players, 60);

  // 恢复默认全局配额，不影响其他测试
  ServerConfig::mutableInstance().max_players = orig_max;
  ServerConfig::mutableInstance().lobby_max_players = orig_lobby_max;
  std::filesystem::remove(tmp_settings);
}
