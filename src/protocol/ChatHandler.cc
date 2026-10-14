/**
 * @file ChatHandler.cc
 * @brief 聊天多播与快捷指令路由系统实现
 */

#include "protocol/ChatHandler.h"

#include <spdlog/spdlog.h>

#include <format>
#include <utility>

#include "protocol/ClientManager.h"
#include "protocol/ClientSession.h"
#include "protocol/PlayHandler.h"
#include "world/World.h"

namespace mcpe::protocol {

ChatHandler::ChatHandler(
    SystemMessageSender sys_sender,
    MulticastSender multicast_sender,
    std::weak_ptr<ClientManager> client_manager
)
    : sys_sender_(std::move(sys_sender)),
      multicast_sender_(std::move(multicast_sender)),
      client_manager_(std::move(client_manager)) {}

void
ChatHandler::setSenders(SystemMessageSender sys_sender, MulticastSender multicast_sender) {
  sys_sender_ = std::move(sys_sender);
  multicast_sender_ = std::move(multicast_sender);
}

void
ChatHandler::setClientManager(std::weak_ptr<ClientManager> client_manager) {
  client_manager_ = std::move(client_manager);
}

void
ChatHandler::setSpawnCallback(SetSpawnCallback cb) {
  spawn_callback_ = std::move(cb);
}

void
ChatHandler::handleMessage(
    uint64_t sender_sid,
    std::string_view sender_name,
    const std::string& raw_msg
) {
  if (raw_msg.empty()) {
    return;
  }

  // 1. 快捷指令分流拦截
  if (raw_msg[0] == '/') {
    handleCommand(sender_sid, raw_msg);
    return;
  }

  // 2. 普通玩家公屏聊天富文本 JSON 格式化
  const std::string escaped_text = escapeJson(raw_msg);
  const std::string json_chat = std::format(
      R"({{"text":"<{}> {}","color":"white"}})",
      escapeJson(sender_name),
      escaped_text
  );

  spdlog::info("[Chat] <{}> {}", sender_name, raw_msg);

  auto chat_packet = buildChatPacket(json_chat, ChatPosition::ChatBox);

  // 3. 全局广播（包括发送者自己）
  if (multicast_sender_) {
    multicast_sender_(std::move(chat_packet));
  }
}

void
ChatHandler::handleCommand(uint64_t sender_sid, const std::string& cmd_line) {
  spdlog::info("[Command] SID {} executed: {}", sender_sid, cmd_line);

  if (cmd_line == "/lobby" || cmd_line == "/spawn") {
    handleSpawnCommand(sender_sid);
  } else if (cmd_line == "/setspawn" || cmd_line == "/setworldspawn") {
    handleSetSpawnCommand(sender_sid);
  } else if (cmd_line == "/maps") {
    sendSystemMessage(sender_sid, "§e[地图列表] 1. 新手跑酷 (输入 /join 1)  2. 螺旋跑酷 (输入 /join 2)");
  } else if (cmd_line == "/fly" || cmd_line.starts_with("/fly ")) {
    handleFlyCommand(sender_sid, cmd_line);
  } else if (cmd_line == "/help") {
    sendSystemMessage(sender_sid, "§e[帮助] 支持指令: /spawn, /setspawn, /fly [on|off], /maps, /lobby");
  } else {
    sendSystemMessage(sender_sid, "§c[错误] 未知指令，输入 /help 查看支持指令。");
  }
}

void
ChatHandler::handleSetSpawnCommand(uint64_t sender_sid) {
  auto mgr = client_manager_.lock();
  if (!mgr) {
    sendSystemMessage(sender_sid, "§c[错误] 会话管理器未就绪");
    return;
  }

  auto session = mgr->findSession(sender_sid);
  if (!session || session->state() != ConnectionState::PLAY) {
    sendSystemMessage(sender_sid, "§c[错误] 只能在游戏主世界中设置出生点");
    return;
  }

  const Vector3d pos = session->position();
  const Rotation rot = session->rotation();

  auto world = PlayHandler::getWorld();
  if (world) {
    world->setSpawnPoint(pos);
    world->setSpawnRotation(rot);
  }

  if (spawn_callback_) {
    spawn_callback_(pos, rot);
  }

  spdlog::info(
      "[Command] Player {} (SID {}) updated lobby spawn point to ({:.2f}, {:.2f}, {:.2f})",
      session->username(),
      sender_sid,
      pos.x,
      pos.y,
      pos.z
  );

  sendSystemMessage(
      sender_sid,
      std::format("§a[系统] 成功将大厅出生点更新为: ({:.1f}, {:.1f}, {:.1f})！已保存至配置文件。", pos.x, pos.y, pos.z)
  );
}

void
ChatHandler::handleSpawnCommand(uint64_t sender_sid) {
  auto mgr = client_manager_.lock();
  if (!mgr) {
    sendSystemMessage(sender_sid, "§c[错误] 会话管理器未就绪");
    return;
  }

  auto session = mgr->findSession(sender_sid);
  if (!session || session->state() != ConnectionState::PLAY) {
    sendSystemMessage(sender_sid, "§c[错误] 只能在游戏主世界中使用传送指令");
    return;
  }

  auto world = PlayHandler::getWorld();
  const Vector3d target = world ? world->spawnPoint() : Vector3d{0.5, 4.0, 0.5};
  const Rotation rot = world ? world->spawnRotation() : Rotation{0.0f, 0.0f};

  const int32_t tid = session->nextTeleportId();
  PlayHandler::sendPlayerPositionAndLook(
      session,
      target.x,
      target.y,
      target.z,
      rot.yaw,
      rot.pitch,
      0x00,
      tid
  );

  sendSystemMessage(sender_sid, "§a[系统] 已传送回大厅出生点！");
}

void
ChatHandler::handleFlyCommand(uint64_t sender_sid, const std::string& cmd_line) {
  auto mgr = client_manager_.lock();
  if (!mgr) {
    sendSystemMessage(sender_sid, "§c[错误] 会话管理器未就绪");
    return;
  }

  auto session = mgr->findSession(sender_sid);
  if (!session || session->state() != ConnectionState::PLAY) {
    sendSystemMessage(sender_sid, "§c[错误] 只能在游戏主世界中开启飞行模式");
    return;
  }

  bool target_fly = !session->canFly();
  if (cmd_line == "/fly on" || cmd_line == "/fly 1" || cmd_line == "/fly true") {
    target_fly = true;
  } else if (cmd_line == "/fly off" || cmd_line == "/fly 0" || cmd_line == "/fly false") {
    target_fly = false;
  } else if (cmd_line != "/fly") {
    sendSystemMessage(sender_sid, "§e[用法] /fly [on|off] - 开启或关闭大厅飞行漫游");
    return;
  }

  session->setCanFly(target_fly);
  session->setIsFlying(target_fly);

  if (target_fly) {
    const uint8_t flags = PlayerAbilitiesFlags::AllowFlying |
                          PlayerAbilitiesFlags::Flying |
                          PlayerAbilitiesFlags::Invulnerable;
    PlayHandler::sendPlayerAbilities(session, flags);
    sendSystemMessage(sender_sid, "§a[系统] 飞行模式已开启！(按空格起飞，Shift 下降，再次输入 /fly 关闭)");
    spdlog::info("[Command] Player {} (SID {}) enabled flight mode", session->username(), sender_sid);
  } else {
    const uint8_t flags = PlayerAbilitiesFlags::Invulnerable;
    PlayHandler::sendPlayerAbilities(session, flags);
    sendSystemMessage(sender_sid, "§c[系统] 飞行模式已关闭。");
    spdlog::info("[Command] Player {} (SID {}) disabled flight mode", session->username(), sender_sid);
  }
}

void
ChatHandler::sendSystemMessage(uint64_t session_id, std::string_view message) {
  const std::string json_msg = std::format(R"({{"text":"{}"}})", escapeJson(message));
  auto packet = buildChatPacket(json_msg, ChatPosition::SystemMessage);
  if (sys_sender_) {
    sys_sender_(session_id, std::move(packet));
  }
}

std::string
ChatHandler::escapeJson(std::string_view text) {
  std::string result;
  result.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\b':
        result += "\\b";
        break;
      case '\f':
        result += "\\f";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          // 过滤掉不可见控制字符
        } else {
          result.push_back(c);
        }
        break;
    }
  }
  return result;
}

ByteBuffer
ChatHandler::buildChatPacket(std::string_view json_text, ChatPosition pos) {
  ByteBuffer buf;
  buf.appendVarInt(ClientboundPacket::ChatMessage);
  buf.appendString(json_text);
  buf.appendByte(static_cast<uint8_t>(pos));

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

}  // namespace mcpe::protocol
