/**
 * @file main.cc
 * @brief MCPE (Minecraft Java Edition 1.12.2 / Protocol 340) 服务端主程序入口
 *
 * 负责加载 settings.ini 与 <map>.ini 配置体系，极速装载预烘焙大厅与跑酷地图镜像 (.mcpe_map)，
 * 启动多实例并发 Actor 引擎 (InstanceManager) 与 Asio 异步网络事件循环。
 */

#include <spdlog/spdlog.h>

#include <asio.hpp>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>

#include "common/IniConfig.h"
#include "game/InstanceManager.h"
#include "game/parkour/ParkourMapConfig.h"
#include "network/TcpServer.h"
#include "protocol/ChatHandler.h"
#include "protocol/ClientManager.h"
#include "protocol/ClientSession.h"
#include "protocol/GameTypes.h"
#include "protocol/PlayHandler.h"
#include "world/World.h"

namespace {
constexpr std::string_view kSettingsIniPath = "src/settings.ini";
constexpr std::string_view kLobbyBakedMapPath = "assets/maps/lobby/floating-island.mcpe_map";
constexpr std::string_view kLobbyBakedMapFallbackPath = "assets/maps/lobby.mcpe_map";
constexpr std::string_view kLobbyIniPath = "assets/maps/lobby.ini";
}  // namespace

using namespace mcpe;
using namespace mcpe::network;
using namespace mcpe::protocol;
using namespace mcpe::world;
using namespace mcpe::game;

int
main() {
  spdlog::set_level(spdlog::level::info);
  spdlog::info("============================================================");
  spdlog::info(" MCPE Server (Protocol 340 / MC 1.12.2 / Multi-Instance)    ");
  spdlog::info("============================================================");

  try {
    // 1. 加载或自动初始化 src/settings.ini
    auto& server_cfg = ServerConfig::mutableInstance();
    if (!std::filesystem::exists(kSettingsIniPath)) {
      (void)server_cfg.saveToIni(kSettingsIniPath);
      spdlog::info("[Main] Generated default global configuration at {}", kSettingsIniPath);
    } else if (server_cfg.loadFromIni(kSettingsIniPath)) {
      spdlog::info(
          "[Main] Loaded {}: port={}, server_max={}, lobby_max={}, max_lobbies={}",
          kSettingsIniPath,
          server_cfg.port,
          server_cfg.max_players,
          server_cfg.lobby_max_players,
          server_cfg.max_lobby_instances
      );
    }

    asio::io_context io_context;
    auto client_manager = std::make_shared<ClientManager>();

    auto lobby_world = std::make_shared<World>();
    std::filesystem::path actual_map_path = kLobbyBakedMapPath;
    if (!std::filesystem::exists(actual_map_path) &&
        std::filesystem::exists(kLobbyBakedMapFallbackPath)) {
      actual_map_path = kLobbyBakedMapFallbackPath;
    }
    const std::string map_path_str = actual_map_path.string();
    if (lobby_world->loadBakedMap(actual_map_path) == 0) {
      spdlog::warn(
          "[Main] Baked map '{}' not found; falling back to default flat world.",
          map_path_str
      );
    }
    if (lobby_world->loadSpawnConfig(kLobbyIniPath)) {
      spdlog::info(
          "[Main] Loaded lobby spawn config from {}: ({:.2f}, {:.2f}, {:.2f})",
          kLobbyIniPath,
          lobby_world->spawnPoint().x,
          lobby_world->spawnPoint().y,
          lobby_world->spawnPoint().z
      );
    } else {
      (void)lobby_world->saveSpawnConfig(kLobbyIniPath);
    }
    PlayHandler::setWorld(lobby_world);

    // 3. 初始化多实例并发调度中心 (InstanceManager)
    auto instance_manager = std::make_shared<InstanceManager>(
        lobby_world,
        static_cast<size_t>(server_cfg.lobby_max_players),
        static_cast<size_t>(server_cfg.max_lobby_instances),
        static_cast<size_t>(server_cfg.parkour_max_players),
        true
    );

    instance_manager->setUnicastCallback([client_manager](uint64_t sid, ByteBuffer packet) {
      client_manager->sendPacketTo(sid, std::move(packet));
    });

    instance_manager->setTeleportCallback(
        [client_manager](uint64_t sid, const Vector3d& pos, const Rotation& rot) {
          if (auto session = client_manager->findSession(sid)) {
            session->setPosition(pos);
            session->setRotation(rot);
            const int32_t tid = session->nextTeleportId();
            PlayHandler::sendPlayerPositionAndLook(
                session,
                pos.x,
                pos.y,
                pos.z,
                rot.yaw,
                rot.pitch,
                0x00,
                tid
            );
          }
        }
    );

    instance_manager->setWorldSwitchCallback(
        [client_manager](uint64_t sid, const std::shared_ptr<IGameInstance>& target_inst) {
          if (auto session = client_manager->findSession(sid)) {
            const bool already_same = (session->currentInstanceId() == target_inst->instanceId());
            session->setCurrentInstanceId(target_inst->instanceId());
            session->setEventQueue(target_inst->eventQueuePtr());
            if (!already_same) {
              PlayHandler::switchPlayerWorld(session, target_inst->world(), GameMode::Adventure);
            }
          }
        }
    );

    // 4. 自动扫描并加载 settings.ini 中配置的跑酷地图列表 (例如 spiral)
    const auto parkour_map_names = common::IniConfig::splitCsv(server_cfg.parkour_maps);
    for (const auto& map_name : parkour_map_names) {
      const std::filesystem::path ini_path = std::format("assets/maps/{}.ini", map_name);
      auto pk_cfg_opt = parkour::ParkourMapConfig::loadFromIni(map_name, ini_path);
      parkour::ParkourMapConfig pk_cfg = pk_cfg_opt.value_or(parkour::ParkourMapConfig{});
      pk_cfg.map_name = map_name;

      auto pk_world = std::make_shared<World>();
      const std::string pk_baked_str = pk_cfg.baked_file.string();
      if (pk_world->loadBakedMap(pk_cfg.baked_file) == 0) {
        spdlog::warn(
            "[Main] Parkour baked map '{}' not found; generating default course chunks.",
            pk_baked_str
        );
      }
      pk_world->setSpawnPoint(pk_cfg.spawn_point);
      pk_world->setSpawnRotation(pk_cfg.spawn_rotation);
      pk_world->setFallResetY(pk_cfg.fall_reset_y);

      instance_manager->registerParkourMap(pk_cfg, pk_world);
    }

    PlayHandler::setInstanceManager(instance_manager);

    // 5. 初始化聊天多播与快捷指令路由系统
    auto chat_handler = std::make_shared<ChatHandler>();
    chat_handler->setClientManager(client_manager);
    chat_handler->setInstanceManager(instance_manager);
    chat_handler->setSettingsPath(kSettingsIniPath);
    chat_handler->setSenders(
        [client_manager](uint64_t session_id, ByteBuffer packet) {
          client_manager->sendPacketTo(session_id, std::move(packet));
        },
        [client_manager](ByteBuffer packet) {
          client_manager->broadcast(std::move(packet));
        }
    );
    chat_handler->setSpawnCallback([lobby_world](const Vector3d& pos, const Rotation& rot) {
      lobby_world->setSpawnPoint(pos);
      lobby_world->setSpawnRotation(rot);
      if (lobby_world->saveSpawnConfig(kLobbyIniPath)) {
        spdlog::info("[Main] Persisted custom lobby spawn point to {}", kLobbyIniPath);
      }
    });

    instance_manager->setChatDispatchCallback(
        [chat_handler](uint64_t sid, std::string_view name, const std::string& msg) {
          chat_handler->handleMessage(sid, name, msg);
        }
    );

    // 6. 启动异步 TCP 服务器监听
    TcpServer server(io_context, server_cfg.port);
    server.setSessionFactory([client_manager](const std::shared_ptr<TcpSession>& tcp) {
      auto session = std::make_shared<ClientSession>(tcp);
      session->setClientManager(client_manager);
      client_manager->addSession(session);
      return session;
    });

    server.start();
    io_context.run();
    instance_manager->stopAll();
  } catch (const std::exception& e) {
    spdlog::critical("Server fatal error: {}", e.what());
    return 1;
  }

  return 0;
}
