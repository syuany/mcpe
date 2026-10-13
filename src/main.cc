/**
 * @file main.cc
 * @brief MCPE (Minecraft Pocket / Java Edition) 服务端主程序入口
 *
 * 负责启动 Asio 事件循环，在 0.0.0.0:25566 监听原版 Minecraft 1.12.2 (Protocol 340) 客户端连入。
 */

#include <spdlog/spdlog.h>

#include <asio.hpp>
#include <cstdint>
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
}

using namespace mcpe;
using namespace mcpe::network;
using namespace mcpe::protocol;
using namespace mcpe::world;

int
main() {
  // 设置全局日志输出级别为 DEBUG，便于开发与调试观测
  spdlog::set_level(spdlog::level::debug);
  spdlog::info("=========================================");
  spdlog::info(" MCPE Server (Protocol 340 / MC 1.12.2) ");
  spdlog::info("=========================================");

  try {
    // 1. 初始化核心 Asio IO 上下文
    asio::io_context io_context;

    // 2. 初始化全局客户端会话管理器
    auto client_manager = std::make_shared<ClientManager>();

    // 3. 初始化跨线程 MPSC 事件队列
    auto event_queue = std::make_shared<EventQueue>();

    // 4. 初始化聊天多播与快捷指令路由系统
    auto chat_handler = std::make_shared<ChatHandler>();
    chat_handler->setSenders(
        [client_manager](uint64_t session_id, ByteBuffer packet) {
          client_manager->sendPacketTo(session_id, std::move(packet));
        },
        [client_manager](ByteBuffer packet) {
          client_manager->broadcast(std::move(packet));
        }
    );

    // 5. 初始化实体管理器与空间增量广播系统
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

    // 6. 初始化大厅世界并预加载浮空岛建筑蓝图
    auto world = std::make_shared<World>();
    if (world->loadLobbySchematic("assets/maps/lobby.schematic", BlockPos{0, 0, 0})) {
      // 96x48x96 浮空岛中心位于 (48, 48)，广场地面位于 Y=32，安全站立点位于 Y=34.0
      world->setSpawnPoint(Vector3d{48.5, 34.0, 48.5});
      entity_manager->setDefaultSpawn(world->spawnPoint());
      spdlog::info("[Main] Loaded floating island lobby schematic, spawn set to (48.5, 34.0, 48.5)");
    } else {
      spdlog::warn("[Main] Failed to load lobby schematic, falling back to default flat world");
    }
    PlayHandler::setWorld(world);

    // 7. 初始化并启动 20 TPS 物理主循环节拍器
    auto world_ticker = std::make_shared<WorldTicker>(
        *event_queue,
        *entity_manager,
        [chat_handler](uint64_t sid, std::string_view name, const std::string& msg) {
          chat_handler->handleMessage(sid, name, msg);
        }
    );
    world_ticker->start();

    // 8. 使用独立端口监听
    TcpServer server(io_context, kServerPort);

    // 会话工厂：挂接 ClientManager 与 EventQueue
    server.setSessionFactory([client_manager, event_queue](const std::shared_ptr<TcpSession>& tcp) {
      auto session = std::make_shared<ClientSession>(tcp);
      session->setClientManager(client_manager);
      session->setEventQueue(event_queue);
      client_manager->addSession(session);
      return session;
    });

    server.start();

    // 8. 阻塞主线程运行异步网络事件循环
    io_context.run();

    // 9. 停机清理
    world_ticker->stop();
  } catch (const std::exception& e) {
    spdlog::critical("Server fatal error: {}", e.what());
    return 1;
  }

  return 0;
}
