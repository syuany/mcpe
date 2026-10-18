/**
 * @file ChatHandler.cc
 * @brief 聊天多播与快捷指令路由系统实现
 */

#include "protocol/ChatHandler.h"

#include <spdlog/spdlog.h>

#include <charconv>
#include <cctype>
#include <format>
#include <sstream>
#include <utility>

#include "common/IniConfig.h"
#include "lobby/LobbyManager.h"
#include "network/PacketWriter.h"
#include "protocol/ClientManager.h"
#include "protocol/ClientSession.h"
#include "protocol/GameTypes.h"
#include "protocol/PlayHandler.h"
#include "world/EntityManager.h"
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
ChatHandler::setEntityManager(std::weak_ptr<world::EntityManager> entity_manager) {
  entity_manager_ = std::move(entity_manager);
}

void
ChatHandler::setLobbyManager(std::weak_ptr<lobby::LobbyManager> lobby_manager) {
  lobby_manager_ = std::move(lobby_manager);
}

void
ChatHandler::setSettingsPath(std::filesystem::path settings_path) {
  settings_path_ = std::move(settings_path);
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

  // 3. 若绑定了 LobbyManager，按玩家当前所在大厅分线隔离广播；否则回退至全局广播
  if (auto lobby_mgr = lobby_manager_.lock()) {
    if (auto lobby = lobby_mgr->getPlayerLobby(sender_sid)) {
      lobby->broadcastInInstance(chat_packet, 0);
      return;
    }
  }

  if (multicast_sender_) {
    multicast_sender_(std::move(chat_packet));
  }
}

void
ChatHandler::handleCommand(uint64_t sender_sid, const std::string& cmd_line) {
  spdlog::info("[Command] SID {} executed: {}", sender_sid, cmd_line);

  const std::string_view trimmed = common::IniConfig::trim(cmd_line);
  const size_t space_pos = trimmed.find(' ');
  const std::string_view cmd =
      (space_pos == std::string_view::npos) ? trimmed : trimmed.substr(0, space_pos);
  const std::string_view arg = (space_pos == std::string_view::npos)
                                   ? std::string_view{}
                                   : common::IniConfig::trim(trimmed.substr(space_pos + 1));

  if (cmd == "/spawn") {
    handleSpawnCommand(sender_sid);
  } else if (cmd == "/lobby" || cmd == "/hub" || cmd == "/leave") {
    handleLobbyCommand(sender_sid, arg);
  } else if (cmd == "/lobbies") {
    handleLobbiesCommand(sender_sid);
  } else if (cmd == "/join") {
    handleJoinCommand(sender_sid, arg);
  } else if (cmd == "/admin") {
    const size_t sub_sp = arg.find(' ');
    const std::string_view sub_cmd =
        (sub_sp == std::string_view::npos) ? arg : arg.substr(0, sub_sp);
    const std::string_view sub_arg =
        (sub_sp == std::string_view::npos) ? std::string_view{} : common::IniConfig::trim(arg.substr(sub_sp + 1));
    handleAdminCommand(sender_sid, sub_cmd, sub_arg);
  } else if (cmd == "/auth") {
    handleAdminCommand(sender_sid, "auth", arg);
  } else if (cmd == "/deauth") {
    handleAdminCommand(sender_sid, "deauth", arg);
  } else if (cmd == "/setspawn" || cmd == "/setworldspawn") {
    handleSetSpawnCommand(sender_sid);
  } else if (cmd == "/setmaxplayers") {
    handleSetMaxPlayersCommand(sender_sid, arg);
  } else if (cmd == "/fly") {
    handleFlyCommand(sender_sid, std::string(trimmed));
  } else if (cmd == "/help") {
    sendSystemMessage(
        sender_sid,
        "§e[帮助] 支持指令: /lobby [id], /lobbies, /join <分线>, /spawn, "
        "/fly [on|off], /setspawn, /setmaxplayers <total> [lobby], /admin auth <口令>, /admin deauth"
    );
  } else {
    sendSystemMessage(sender_sid, "§c[错误] 未知指令，输入 /help 查看支持指令。");
  }
}

void
ChatHandler::handleLobbiesCommand(uint64_t sender_sid) {
  auto lobby_mgr = lobby_manager_.lock();
  if (!lobby_mgr) {
    sendSystemMessage(sender_sid, "§e[大厅分线] 当前为单实例模式: lobby-1");
    return;
  }

  const auto lobbies = lobby_mgr->getLobbies();
  std::ostringstream oss;
  oss << "§e[大厅分线列表] 共 " << lobbies.size() << " 条分线: ";
  for (size_t i = 0; i < lobbies.size(); ++i) {
    if (i > 0) oss << " | ";
    oss << "§b" << lobbies[i]->instanceId() << " §7[" << lobbies[i]->playerCount() << "/"
        << lobbies[i]->maxPlayers() << "]";
  }
  sendSystemMessage(sender_sid, oss.str());
}

void
ChatHandler::handleJoinCommand(uint64_t sender_sid, std::string_view shard_arg) {
  if (shard_arg.empty()) {
    sendSystemMessage(sender_sid, "§e[用法] /join <分线序号> (例如: /join 2，输入 /lobbies 查看分线)");
    return;
  }
  handleLobbyCommand(sender_sid, shard_arg);
}

void
ChatHandler::handleLobbyCommand(uint64_t sender_sid, std::string_view shard_arg) {
  auto lobby_mgr = lobby_manager_.lock();
  auto mgr = client_manager_.lock();
  if (!lobby_mgr || !mgr) {
    sendSystemMessage(sender_sid, "§a[系统] 您已经在主大厅中！输入 /spawn 可传送至大厅出生点。");
    return;
  }

  auto session = mgr->findSession(sender_sid);
  if (!session || session->state() != ConnectionState::PLAY) {
    sendSystemMessage(sender_sid, "§a[系统] 您已经在主大厅中！输入 /spawn 可传送至大厅出生点。");
    return;
  }

  std::shared_ptr<lobby::LobbyInstance> target_lobby;
  if (!shard_arg.empty()) {
    std::string target_id = std::string(shard_arg);
    if (!target_id.starts_with("lobby-")) {
      target_id = "lobby-" + target_id;
    }
    target_lobby = lobby_mgr->findLobby(target_id);
    if (!target_lobby) {
      sendSystemMessage(
          sender_sid,
          std::format("§c[错误] 大厅分线 '{}' 不存在！输入 /lobbies 查看活跃分线。", target_id)
      );
      return;
    }
    if (target_lobby->isFull() && !target_lobby->hasReservedSlot(sender_sid)) {
      sendSystemMessage(
          sender_sid,
          std::format("§c[错误] 大厅分线 '{}' 已满员 ({}/{})！",
                      target_id,
                      target_lobby->playerCount(),
                      target_lobby->maxPlayers())
      );
      return;
    }
  } else {
    auto cur_lobby = lobby_mgr->getPlayerLobby(sender_sid);
    sendSystemMessage(
        sender_sid,
        std::format("§a[系统] 您当前处于 {} 分线！输入 /lobbies 查看所有分线。",
                    cur_lobby ? cur_lobby->instanceId() : "主大厅")
    );
    return;
  }

  if (target_lobby &&
      lobby_mgr->transferPlayer(
          sender_sid,
          session->entityId(),
          session->username(),
          session->uuidBytes(),
          target_lobby
      )) {
    sendSystemMessage(
        sender_sid,
        std::format("§a[系统] 已切换至大厅分线 §b{}§a！", target_lobby->instanceId())
    );
  } else {
    sendSystemMessage(sender_sid, "§a[系统] 您已经在主大厅中！输入 /spawn 可传送至大厅出生点。");
  }
}

void
ChatHandler::handleSetMaxPlayersCommand(uint64_t sender_sid, std::string_view args) {
  if (auto mgr = client_manager_.lock()) {
    auto session = mgr->findSession(sender_sid);
    if (session && !isPlayerAdmin(session)) {
      sendSystemMessage(
          sender_sid,
          "§c[权限不足] 仅管理员可调整服务器人数上限！输入 /admin auth <口令> 鉴权。"
      );
      return;
    }
  }

  if (args.empty()) {
    sendSystemMessage(
        sender_sid,
        "§e[用法] /setmaxplayers <全服上限> [单大厅分线上限] (自动保存至 src/settings.ini)"
    );
    return;
  }

  const size_t sp = args.find(' ');
  const std::string_view first_tok =
      (sp == std::string_view::npos) ? args : args.substr(0, sp);
  const std::string_view second_tok =
      (sp == std::string_view::npos) ? std::string_view{} : common::IniConfig::trim(args.substr(sp + 1));

  int32_t total_max = 0;
  auto [p1, ec1] = std::from_chars(first_tok.data(), first_tok.data() + first_tok.size(), total_max);
  if (ec1 != std::errc{} || total_max <= 0) {
    sendSystemMessage(sender_sid, "§c[错误] 全服最大人数必须为正整数！");
    return;
  }

  auto& cfg = ServerConfig::mutableInstance();
  cfg.max_players = total_max;

  if (!second_tok.empty()) {
    int32_t lobby_max = 0;
    auto [p2, ec2] =
        std::from_chars(second_tok.data(), second_tok.data() + second_tok.size(), lobby_max);
    if (ec2 == std::errc{} && lobby_max > 0) {
      cfg.lobby_max_players = lobby_max;
      if (auto lobby_mgr = lobby_manager_.lock()) {
        lobby_mgr->setLobbyMaxPlayers(static_cast<size_t>(lobby_max));
      }
    }
  }

  const bool saved = cfg.saveToIni(settings_path_);
  sendSystemMessage(
      sender_sid,
      std::format(
          "§a[系统] 已更新人数上限: 全服={} 人, 单大厅分线={} 人 ({})",
          cfg.max_players,
          cfg.lobby_max_players,
          saved ? "已持久化至 src/settings.ini" : "内存已生效"
      )
  );
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

  Vector3d pos = session->position();
  Rotation rot = session->rotation();

  if (auto lobby_mgr = lobby_manager_.lock()) {
    if (auto lobby = lobby_mgr->getPlayerLobby(sender_sid)) {
      if (const auto* player_ent = lobby->entityManager().getPlayer(sender_sid)) {
        pos = player_ent->position;
        rot = player_ent->rotation;
      }
      lobby->worldPtr()->setSpawnPoint(pos);
      lobby->worldPtr()->setSpawnRotation(rot);
      lobby->entityManager().setDefaultSpawn(pos, rot);
    }
  } else if (auto em = entity_manager_.lock()) {
    if (const auto* player_ent = em->getPlayer(sender_sid)) {
      pos = player_ent->position;
      rot = player_ent->rotation;
    }
  }

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
      std::format(
          "§a[系统] 成功将大厅出生点更新为: ({:.1f}, {:.1f}, {:.1f})！已保存至配置文件。",
          pos.x,
          pos.y,
          pos.z
      )
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

  std::shared_ptr<world::World> world = PlayHandler::getWorld();
  std::shared_ptr<world::EntityManager> em = entity_manager_.lock();
  if (auto lobby_mgr = lobby_manager_.lock()) {
    if (auto lobby = lobby_mgr->getPlayerLobby(sender_sid)) {
      world = lobby->worldPtr();
      em = lobby->entityManagerPtr();
    }
  }

  const Vector3d target = world ? world->spawnPoint() : Vector3d{0.5, 4.0, 0.5};
  const Rotation rot = world ? world->spawnRotation() : Rotation{0.0f, 0.0f};

  session->setPosition(target);
  session->setRotation(rot);
  if (em) {
    em->updatePlayerPosition(sender_sid, target, rot, true);
  }

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
    sendSystemMessage(
        sender_sid,
        "§a[系统] 飞行模式已开启！(按空格起飞，Shift 下降，再次输入 /fly 关闭)"
    );
    spdlog::info(
        "[Command] Player {} (SID {}) enabled flight mode",
        session->username(),
        sender_sid
    );
  } else {
    const uint8_t flags = PlayerAbilitiesFlags::Invulnerable;
    PlayHandler::sendPlayerAbilities(session, flags);
    sendSystemMessage(sender_sid, "§c[系统] 飞行模式已关闭。");
    spdlog::info(
        "[Command] Player {} (SID {}) disabled flight mode",
        session->username(),
        sender_sid
    );
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
  return lobby::LobbyInstance::escapeJsonText(text);
}

ByteBuffer
ChatHandler::buildChatPacket(std::string_view json_text, ChatPosition pos) {
  return lobby::LobbyInstance::buildChatPacket(json_text, pos);
}

bool
ChatHandler::isPlayerAdmin(const std::shared_ptr<ClientSession>& session) const {
  if (!session) {
    return false;
  }
  if (session->isAdmin()) {
    return true;
  }

  const auto& cfg = ServerConfig::defaultInstance();
  const auto admin_list = common::IniConfig::splitCsv(cfg.admins);
  const std::string& uname = session->username();
  const std::string& uid = session->uuid();

  auto iequals = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
          std::tolower(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
  };

  for (const auto& item : admin_list) {
    if (item.empty()) continue;
    if ((!uname.empty() && iequals(item, uname)) ||
        (!uid.empty() && iequals(item, uid))) {
      session->setAdmin(true);
      return true;
    }
  }

  return false;
}

void
ChatHandler::handleAdminCommand(
    uint64_t sender_sid,
    std::string_view sub_cmd,
    std::string_view arg
) {
  auto mgr = client_manager_.lock();
  if (!mgr) {
    sendSystemMessage(sender_sid, "§c[错误] 会话管理器未就绪");
    return;
  }

  auto session = mgr->findSession(sender_sid);
  if (!session || session->state() != ConnectionState::PLAY) {
    sendSystemMessage(sender_sid, "§c[错误] 只能在游戏主世界中执行该指令");
    return;
  }

  const auto& cfg = ServerConfig::defaultInstance();

  // 1. 口令提权鉴权逻辑 (/admin auth <secret>)
  if (sub_cmd == "auth") {
    if (!cfg.dev_mode_enabled) {
      sendSystemMessage(sender_sid, "§c[系统] 开发者与管理模式已在服务端配置中全局禁用。");
      return;
    }
    if (session->isAdmin()) {
      sendSystemMessage(sender_sid, "§e[鉴权提示] 您当前已具备开发者与管理员权限，无需重复鉴权。");
      return;
    }
    if (cfg.dev_secret.empty()) {
      sendSystemMessage(sender_sid, "§c[错误] 服务端未启用动态口令鉴权（dev_secret 为空），仅支持白名单用户。");
      return;
    }
    if (arg.empty()) {
      sendSystemMessage(sender_sid, "§e[用法] /admin auth <口令> (在 src/settings.ini 中配置)");
      return;
    }
    if (arg == cfg.dev_secret) {
      session->setAdmin(true);
      spdlog::info(
          "[Security] Player '{}' (sid={}) authenticated as admin successfully via secret.",
          session->username(), sender_sid
      );
      sendSystemMessage(
          sender_sid,
          "§a[鉴权成功] 已解锁管理员权限！"
      );
    } else {
      spdlog::warn(
          "[Security] Player '{}' (sid={}, IP={}) failed admin auth attempt with key '{}'.",
          session->username(), sender_sid, session->remoteAddress(), arg
      );
      sendSystemMessage(sender_sid, "§c[鉴权失败] 开发者与管理员口令错误！");
    }
    return;
  }

  // 2. 注销权限 (/admin deauth)
  if (sub_cmd == "deauth" || sub_cmd == "logout") {
    session->setAdmin(false);
    sendSystemMessage(sender_sid, "§e[鉴权] 已注销当前会话的开发者管理权限。");
    return;
  }

  // 3. 全局熔断开关检查
  if (!cfg.dev_mode_enabled) {
    sendSystemMessage(sender_sid, "§c[系统] 开发者与管理模式已在服务端配置中全局禁用。");
    return;
  }

  // 4. 管理员身份鉴权校验
  if (!isPlayerAdmin(session)) {
    sendSystemMessage(
        sender_sid,
        "§c[权限不足] 您没有权限使用管理指令！请输入 §b/admin auth <口令> §c进行鉴权。"
    );
    return;
  }

  if (sub_cmd == "dev") {
    const bool enable = (arg != "off" && arg != "false" && arg != "0");
    session->setCanFly(enable);
    session->setIsFlying(enable);
    if (enable) {
      const uint8_t flags = PlayerAbilitiesFlags::AllowFlying |
                            PlayerAbilitiesFlags::Flying |
                            PlayerAbilitiesFlags::Invulnerable;
      PlayHandler::sendPlayerAbilities(session, flags, 0.08f, 0.1f);
      sendSystemMessage(
          sender_sid,
          "§a[开发者模式] 开发者模式已启用！已允许飞行。"
      );
    } else {
      PlayHandler::sendPlayerAbilities(session, 0x00, 0.05f, 0.1f);
      sendSystemMessage(sender_sid, "§e[开发者模式] 开发者模式已关闭。");
    }
    return;
  }

  sendSystemMessage(
      sender_sid,
      "§e[管理指令] 支持指令: /admin auth <口令>, /admin deauth, /admin dev [on|off]"
  );
}

}  // namespace mcpe::protocol
