/**
 * @file ChatHandler.cc
 * @brief 聊天多播与快捷指令路由系统实现
 */

#include "protocol/ChatHandler.h"

#include <spdlog/spdlog.h>

#include <charconv>
#include <format>
#include <sstream>
#include <utility>

#include "common/IniConfig.h"
#include "game/InstanceManager.h"
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
ChatHandler::setInstanceManager(std::weak_ptr<game::InstanceManager> instance_manager) {
  instance_manager_ = std::move(instance_manager);
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

  // 3. 若绑定了 InstanceManager，按玩家当前所在实例隔离广播；否则回退至全局广播
  if (auto inst_mgr = instance_manager_.lock()) {
    if (auto inst = inst_mgr->getPlayerInstance(sender_sid)) {
      inst->broadcastInInstance(chat_packet, 0);
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
  } else if (cmd == "/join" || cmd == "/parkour") {
    handleJoinCommand(sender_sid, arg);
  } else if (cmd == "/maps") {
    handleMapsCommand(sender_sid);
  } else if (cmd == "/top") {
    handleTopCommand(sender_sid, arg);
  } else if (cmd == "/setspawn" || cmd == "/setworldspawn") {
    handleSetSpawnCommand(sender_sid);
  } else if (cmd == "/setmaxplayers") {
    handleSetMaxPlayersCommand(sender_sid, arg);
  } else if (cmd == "/fly") {
    handleFlyCommand(sender_sid, std::string(trimmed));
  } else if (cmd == "/help") {
    sendSystemMessage(
        sender_sid,
        "§e[帮助] 支持指令: /join <map>, /maps, /top [map], /lobby [id], /lobbies, /spawn, "
        "/setspawn, /setmaxplayers <total> [lobby], /fly [on|off]"
    );
  } else {
    sendSystemMessage(sender_sid, "§c[错误] 未知指令，输入 /help 查看支持指令。");
  }
}

void
ChatHandler::handleMapsCommand(uint64_t sender_sid) {
  if (auto inst_mgr = instance_manager_.lock()) {
    const auto maps = inst_mgr->getRegisteredParkourMaps();
    if (!maps.empty()) {
      std::string list_str;
      for (size_t i = 0; i < maps.size(); ++i) {
        if (i > 0) list_str += ", ";
        list_str += maps[i];
      }
      sendSystemMessage(
          sender_sid,
          std::format(
              "§e[地图列表] 主大厅分线数: {} | 可玩跑酷地图: §b{} §7(输入 /join <地图名> 立即开跑)",
              inst_mgr->lobbyInstanceCount(),
              list_str
          )
      );
      return;
    }
  }
  sendSystemMessage(
      sender_sid,
      "§e[地图列表] 当前常驻世界: 浮空岛主大厅 | 跑酷地图: spiral (使用 /join spiral 进入跑酷)"
  );
}

void
ChatHandler::handleLobbiesCommand(uint64_t sender_sid) {
  auto inst_mgr = instance_manager_.lock();
  if (!inst_mgr) {
    sendSystemMessage(sender_sid, "§e[大厅分线] 当前为单实例模式: lobby-1");
    return;
  }

  const auto lobbies = inst_mgr->getLobbies();
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
ChatHandler::handleJoinCommand(uint64_t sender_sid, std::string_view map_arg) {
  if (map_arg.empty()) {
    sendSystemMessage(sender_sid, "§e[用法] /join <地图名> (例如: /join spiral)");
    return;
  }

  auto mgr = client_manager_.lock();
  auto inst_mgr = instance_manager_.lock();
  if (!mgr || !inst_mgr) {
    sendSystemMessage(sender_sid, "§c[错误] 多实例调度引擎未启用");
    return;
  }

  auto session = mgr->findSession(sender_sid);
  if (!session || session->state() != ConnectionState::PLAY) {
    sendSystemMessage(sender_sid, "§c[错误] 只能在游戏主世界中加入跑酷房间");
    return;
  }

  auto parkour_inst = inst_mgr->acquireParkourForPlayer(sender_sid, map_arg);
  if (!parkour_inst) {
    sendSystemMessage(
        sender_sid,
        std::format("§c[错误] 未找到跑酷地图 '{}'，输入 /maps 查看可用地图。", map_arg)
    );
    return;
  }

  if (inst_mgr->transferPlayer(
          sender_sid,
          session->entityId(),
          session->username(),
          session->uuidBytes(),
          parkour_inst
      )) {
    sendSystemMessage(
        sender_sid,
        std::format("§a[系统] 已通过 0x35 无缝切入跑酷房间 §e{}§a！", parkour_inst->instanceId())
    );
  } else {
    sendSystemMessage(sender_sid, "§e[提示] 您已经在该跑酷房间中！");
  }
}

void
ChatHandler::handleLobbyCommand(uint64_t sender_sid, std::string_view shard_arg) {
  auto inst_mgr = instance_manager_.lock();
  auto mgr = client_manager_.lock();
  if (!inst_mgr || !mgr) {
    sendSystemMessage(sender_sid, "§a[系统] 您已经在主大厅中！输入 /spawn 可传送至大厅出生点。");
    return;
  }

  auto session = mgr->findSession(sender_sid);
  if (!session || session->state() != ConnectionState::PLAY) {
    sendSystemMessage(sender_sid, "§a[系统] 您已经在主大厅中！输入 /spawn 可传送至大厅出生点。");
    return;
  }

  std::shared_ptr<game::IGameInstance> target_lobby;
  if (!shard_arg.empty()) {
    std::string target_id = std::string(shard_arg);
    if (!target_id.starts_with("lobby-")) {
      target_id = "lobby-" + target_id;
    }
    target_lobby = inst_mgr->findInstance(target_id);
    if (!target_lobby || target_lobby->type() != game::InstanceType::Lobby) {
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
    auto cur_inst = inst_mgr->getPlayerInstance(sender_sid);
    if (!cur_inst || cur_inst->type() == game::InstanceType::Lobby) {
      sendSystemMessage(sender_sid, "§a[系统] 您已经在主大厅中！输入 /spawn 可传送至大厅出生点。");
      return;
    }
    target_lobby = inst_mgr->acquireLobbyForPlayer(sender_sid);
  }

  if (target_lobby &&
      inst_mgr->transferPlayer(
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
ChatHandler::handleTopCommand(uint64_t sender_sid, std::string_view map_arg) {
  auto inst_mgr = instance_manager_.lock();
  if (!inst_mgr) {
    sendSystemMessage(sender_sid, "§e[排行榜] 暂无通关记录。");
    return;
  }

  std::string target_map = map_arg.empty() ? "spiral" : std::string(map_arg);
  const std::string inst_id = std::format("parkour-{}-1", target_map);
  auto inst = inst_mgr->findInstance(inst_id);
  auto parkour_inst = std::dynamic_pointer_cast<game::parkour::ParkourInstance>(inst);
  if (!parkour_inst || parkour_inst->leaderboard().empty()) {
    sendSystemMessage(
        sender_sid,
        std::format("§e[排行榜] 赛道 [{}] 暂无通关记录，输入 /join {} 创造首个纪录！", target_map, target_map)
    );
    return;
  }

  std::ostringstream oss;
  oss << "§6[跑酷排行榜 - " << target_map << "] ";
  const auto& board = parkour_inst->leaderboard();
  for (size_t i = 0; i < board.size(); ++i) {
    if (i > 0) oss << " | ";
    oss << std::format(
        "#{} §e{} §b{:.3f}s",
        i + 1,
        board[i].username,
        static_cast<double>(board[i].elapsed_ms) / 1000.0
    );
  }
  sendSystemMessage(sender_sid, oss.str());
}

void
ChatHandler::handleSetMaxPlayersCommand(uint64_t sender_sid, std::string_view args) {
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
      if (auto inst_mgr = instance_manager_.lock()) {
        inst_mgr->setLobbyMaxPlayers(static_cast<size_t>(lobby_max));
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

  if (auto inst_mgr = instance_manager_.lock()) {
    if (auto inst = inst_mgr->getPlayerInstance(sender_sid)) {
      if (const auto* player_ent = inst->entityManager().getPlayer(sender_sid)) {
        pos = player_ent->position;
        rot = player_ent->rotation;
      }
      inst->worldPtr()->setSpawnPoint(pos);
      inst->worldPtr()->setSpawnRotation(rot);
      inst->entityManager().setDefaultSpawn(pos, rot);
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
  if (auto inst_mgr = instance_manager_.lock()) {
    if (auto inst = inst_mgr->getPlayerInstance(sender_sid)) {
      world = inst->worldPtr();
      em = inst->entityManagerPtr();
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

  // 防作弊铁律：跑酷竞速实例内禁止开启飞行模式
  if (auto inst_mgr = instance_manager_.lock()) {
    if (auto inst = inst_mgr->getPlayerInstance(sender_sid)) {
      if (inst->type() == game::InstanceType::Parkour) {
        sendSystemMessage(sender_sid, "§c[防作弊] 跑酷竞速赛道内禁止开启飞行模式！");
        return;
      }
    }
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
  return game::IGameInstance::escapeJsonText(text);
}

ByteBuffer
ChatHandler::buildChatPacket(std::string_view json_text, ChatPosition pos) {
  return game::IGameInstance::buildChatPacket(json_text, pos);
}

}  // namespace mcpe::protocol
