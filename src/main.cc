/**
 * @file main.cc
 * @brief MCPE (Minecraft Java Edition 1.12.2 / Protocol 340) 服务端主程序入口
 *
 * 负责装配四层核心组件，极速加载预烘焙大厅镜像 (lobby.mcpe_map)，
 * 启动 20 TPS 物理节拍器与 Asio 异步网络事件循环。
 */

#include <spdlog/spdlog.h>

#include <asio.hpp>
#include <cstdint>
#include <filesystem>
#include <memory>

#include "network/TcpServer.h"
#include "protocol/ChatHandler.h"
#include "protocol/ClientManager.h"
#include "protocol/ClientSession.h"
#include "protocol/PlayHandler.h"
#include "world/EntityManager.h"
#include "world/EventQueue.h"
#include "world/World.h"
#include "world/WorldTicker.h"

namespace {
constexpr uint16_t kServerPort = 25566;
constexpr std::string_view kBakedMapPath = "assets/maps/lobby.mcpe_map";
constexpr std::string_view kSpawnConfigPath = "assets/maps/lobby/spawn.json";
}  // namespace

using namespace mcpe;
using namespace mcpe::network;
using namespace mcpe::protocol;
using namespace mcpe::world;

int
main() {
  spdlog::set_level(spdlog::level::debug);
  spdlog::info("=========================================");
  spdlog::info(" MCPE Server (Protocol 340 / MC 1.12.2) ");
  spdlog::info("=========================================");

  try {
    // 1. 初始化核心组件：Asio IO 上下文、客户端管理器、跨线程 MPSC 队列与实体管理器
    asio::io_context io_context;
    auto client_manager = std::make_shared<ClientManager>();
    auto event_queue = std::make_shared<EventQueue>();
    auto entity_manager = std::make_shared<EntityManager>();

    entity_manager->setSenders(
        [client_manager](uint64_t session_id, ByteBuffer packet) {
          client_manager->sendPacketTo(session_id, std::move(packet));
        },
        [client_manager](uint64_t except_session_id, ByteBuffer packet) {
          client_manager->broadcastExceptSessionId(std::move(packet), except_session_id);
        },
        [client_manager](ByteBuffer packet) {
          client_manager->broadcast(std::move(packet));
        }
    );

    // 2. 初始化大厅世界并极速加载预烘焙地图镜像 (.mcpe_map)
    auto world = std::make_shared<World>();
    if (world->loadBakedMap(kBakedMapPath) == 0) {
      spdlog::warn(
          "[Main] Baked map '{}' not found; falling back to default flat world. "
          "(Tip: run './bin/map_baker' to bake .zip/.schematic maps offline)",
          kBakedMapPath
      );
    }

    // 3. 加载自定义持久化出生点配置 (spawn.json)
    if (world->loadSpawnConfig(kSpawnConfigPath)) {
      spdlog::info(
          "[Main] Loaded custom spawn config from {}: ({:.2f}, {:.2f}, {:.2f})",
          kSpawnConfigPath,
          world->spawnPoint().x,
          world->spawnPoint().y,
          world->spawnPoint().z
      );
    }
    entity_manager->setDefaultSpawn(world->spawnPoint(), world->spawnRotation());
    PlayHandler::setWorld(world);

    // 4. 初始化聊天多播与快捷指令路由系统
    auto chat_handler = std::make_shared<ChatHandler>();
    chat_handler->setClientManager(client_manager);
    chat_handler->setEntityManager(entity_manager);
    chat_handler->setSenders(
        [client_manager](uint64_t session_id, ByteBuffer packet) {
          client_manager->sendPacketTo(session_id, std::move(packet));
        },
        [client_manager](ByteBuffer packet) {
          client_manager->broadcast(std::move(packet));
        }
    );
    chat_handler->setSpawnCallback(
        [world, entity_manager](const Vector3d& pos, const Rotation& rot) {
          world->setSpawnPoint(pos);
          world->setSpawnRotation(rot);
          entity_manager->setDefaultSpawn(pos, rot);
          if (world->saveSpawnConfig(kSpawnConfigPath)) {
            spdlog::info("[Main] Persisted custom spawn point to {}", kSpawnConfigPath);
          }
        }
    );

    // 5. 启动 20 TPS 物理主循环节拍器
    auto world_ticker = std::make_shared<WorldTicker>(
        *event_queue,
        *entity_manager,
        [chat_handler](uint64_t sid, std::string_view name, const std::string& msg) {
          chat_handler->handleMessage(sid, name, msg);
        }
    );
    world_ticker->start();

    // 6. 启动异步 TCP 服务器监听
    TcpServer server(io_context, kServerPort);
    server.setSessionFactory([client_manager, event_queue](const std::shared_ptr<TcpSession>& tcp) {
      auto session = std::make_shared<ClientSession>(tcp);
      session->setClientManager(client_manager);
      session->setEventQueue(event_queue);
      client_manager->addSession(session);
      return session;
    });

    server.start();
    io_context.run();
    world_ticker->stop();
  } catch (const std::exception& e) {
    spdlog::critical("Server fatal error: {}", e.what());
    return 1;
  }

  return 0;
}
