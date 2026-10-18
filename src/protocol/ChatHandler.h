/**
 * @file ChatHandler.h
 * @brief 聊天多播与快捷指令路由系统（Protocol 协议层）
 *
 * 支持功能：
 * 1. 按玩家当前所属大厅分线（LobbyInstance）隔离公屏聊天多播；
 * 2. 快捷指令路由：/lobby [id], /lobbies, /join <id>, /spawn,
 *    /fly [on|off], /setspawn, /setmaxplayers <server_max> [lobby_max],
 *    /admin auth <secret>, /admin deauth, /help。
 */

#ifndef MCPE_PROTOCOL_CHAT_HANDLER_H
#define MCPE_PROTOCOL_CHAT_HANDLER_H

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "common/ByteBuffer.h"
#include "common/SpatialTypes.h"
#include "protocol/PacketTypes.h"

namespace mcpe::world {
class EntityManager;
}

namespace mcpe::lobby {
class LobbyManager;
}

namespace mcpe::protocol {

class ClientManager;
class ClientSession;

class ChatHandler {
 public:
  using SystemMessageSender = std::function<void(uint64_t session_id, ByteBuffer packet)>;
  using MulticastSender = std::function<void(ByteBuffer packet)>;
  using SetSpawnCallback = std::function<void(const Vector3d& pos, const Rotation& rot)>;

  explicit ChatHandler(
      SystemMessageSender sys_sender = nullptr,
      MulticastSender multicast_sender = nullptr,
      std::weak_ptr<ClientManager> client_manager = {}
  );

  void setSenders(SystemMessageSender sys_sender, MulticastSender multicast_sender);
  void setClientManager(std::weak_ptr<ClientManager> client_manager);
  void setEntityManager(std::weak_ptr<world::EntityManager> entity_manager);
  void setLobbyManager(std::weak_ptr<lobby::LobbyManager> lobby_manager);
  void setSettingsPath(std::filesystem::path settings_path);
  void setSpawnCallback(SetSpawnCallback cb);

  /**
   * @brief 处理来自玩家的消息（区分普通公屏聊天与以 / 开头的快捷指令）
   */
  void handleMessage(
      uint64_t sender_sid,
      std::string_view sender_name,
      const std::string& raw_msg
  );

  /**
   * @brief 处理以 / 开头的控制台/大厅快捷指令（绝不多播广播至公屏，仅私发系统回显）
   */
  void handleCommand(uint64_t sender_sid, const std::string& cmd_line);

  /**
   * @brief 向指定会话单播系统格式富文本提示
   */
  void sendSystemMessage(uint64_t session_id, std::string_view message);

  /**
   * @brief 工具函数：安全转义 JSON 控制字符与双引号
   */
  static std::string escapeJson(std::string_view text);

  /**
   * @brief 构造 0x0F Chat Message (Clientbound) 协议包
   */
  static ByteBuffer buildChatPacket(std::string_view json_text, ChatPosition pos);

 private:
  void handleFlyCommand(uint64_t sender_sid, const std::string& cmd_line);
  void handleSetSpawnCommand(uint64_t sender_sid);
  void handleSpawnCommand(uint64_t sender_sid);
  void handleJoinCommand(uint64_t sender_sid, std::string_view shard_arg);
  void handleLobbyCommand(uint64_t sender_sid, std::string_view shard_arg);
  void handleLobbiesCommand(uint64_t sender_sid);
  void handleSetMaxPlayersCommand(uint64_t sender_sid, std::string_view args);
  void handleAdminCommand(
      uint64_t sender_sid,
      std::string_view sub_cmd,
      std::string_view arg
  );
  [[nodiscard]] bool isPlayerAdmin(const std::shared_ptr<ClientSession>& session) const;

  SystemMessageSender sys_sender_;
  MulticastSender multicast_sender_;
  std::weak_ptr<ClientManager> client_manager_;
  std::weak_ptr<world::EntityManager> entity_manager_;
  std::weak_ptr<lobby::LobbyManager> lobby_manager_;
  std::filesystem::path settings_path_{"src/settings.ini"};
  SetSpawnCallback spawn_callback_;
};

}  // namespace mcpe::protocol

#endif  // MCPE_PROTOCOL_CHAT_HANDLER_H
