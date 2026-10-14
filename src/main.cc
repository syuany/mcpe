/**
 * @file main.cc
 * @brief MCPE (Minecraft Pocket / Java Edition) 服务端主程序入口
 *
 * 负责启动 Asio 事件循环，在 0.0.0.0:25566 监听原版 Minecraft 1.12.2 (Protocol 340) 客户端连入。
 */

#include <spdlog/spdlog.h>

#include <asio.hpp>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

bool
loadSpawnConfig(const std::filesystem::path& path, mcpe::Vector3d& pos, mcpe::Rotation& rot) {
  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }
  std::string line;
  bool has_x = false;
  bool has_y = false;
  bool has_z = false;
  while (std::getline(file, line)) {
    auto parse_field = [&](std::string_view key, auto& out_val) {
      auto pos_key = line.find(key);
      if (pos_key != std::string::npos) {
        auto colon = line.find(':', pos_key + key.size());
        if (colon != std::string::npos) {
          char* end = nullptr;
          const char* str_start = line.c_str() + colon + 1;
          double v = std::strtod(str_start, &end);
          if (end != str_start) {
            out_val = static_cast<std::decay_t<decltype(out_val)>>(v);
            return true;
          }
        }
      }
      return false;
    };
    if (parse_field("\"x\"", pos.x)) has_x = true;
    if (parse_field("\"y\"", pos.y)) has_y = true;
    if (parse_field("\"z\"", pos.z)) has_z = true;
    parse_field("\"yaw\"", rot.yaw);
    parse_field("\"pitch\"", rot.pitch);
  }
  return has_x && has_y && has_z;
}

bool
saveSpawnConfig(const std::filesystem::path& path, const mcpe::Vector3d& pos, const mcpe::Rotation& rot) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }
  file << std::format(
      "{{\n"
      "  \"x\": {:.2f},\n"
      "  \"y\": {:.2f},\n"
      "  \"z\": {:.2f},\n"
      "  \"yaw\": {:.2f},\n"
      "  \"pitch\": {:.2f}\n"
      "}}\n",
      pos.x,
      pos.y,
      pos.z,
      rot.yaw,
      rot.pitch
  );
  return true;
}
}  // namespace

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
    chat_handler->setClientManager(client_manager);
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

    // 6. 初始化大厅世界并预加载原版世界存档或浮空岛建筑蓝图
    auto world = std::make_shared<World>();
    bool lobby_loaded = false;
    const std::filesystem::path lobby_dir = "assets/maps/lobby";
    if (std::filesystem::exists(lobby_dir) && std::filesystem::is_directory(lobby_dir)) {
      for (const auto& entry : std::filesystem::directory_iterator(lobby_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".zip") {
          size_t chunks = world->loadWorldFromZip(entry.path());
          if (chunks > 0) {
            lobby_loaded = true;
            spdlog::info(
                "[Main] Loaded Anvil world from {} ({} chunks), map spawn set to ({:.1f}, {:.1f}, {:.1f})",
                entry.path().filename().string(),
                chunks,
                world->spawnPoint().x,
                world->spawnPoint().y,
                world->spawnPoint().z
            );
            break;
          }
        }
      }
    }
    if (!lobby_loaded && world->loadLobbySchematic("assets/maps/lobby.schematic", BlockPos{0, 0, 0})) {
      // 96x48x96 浮空岛中心位于 (48, 48)，广场地面位于 Y=32，安全站立点位于 Y=34.0
      world->setSpawnPoint(Vector3d{48.5, 34.0, 48.5});
      lobby_loaded = true;
      spdlog::info("[Main] Loaded floating island lobby schematic, default spawn set to (48.5, 34.0, 48.5)");
    }
    if (!lobby_loaded) {
      spdlog::warn("[Main] Failed to load lobby map, falling back to default flat world");
    }

    // 检查并加载自定义持久化出生点 (spawn.json)
    const std::filesystem::path spawn_cfg_path = "assets/maps/lobby/spawn.json";
    Vector3d custom_spawn = world->spawnPoint();
    Rotation custom_rot = world->spawnRotation();
    if (loadSpawnConfig(spawn_cfg_path, custom_spawn, custom_rot)) {
      world->setSpawnPoint(custom_spawn);
      world->setSpawnRotation(custom_rot);
      entity_manager->setDefaultSpawn(custom_spawn, custom_rot);
      spdlog::info(
          "[Main] Loaded custom spawn point from {}: ({:.2f}, {:.2f}, {:.2f}), yaw={:.1f}, pitch={:.1f}",
          spawn_cfg_path.string(),
          custom_spawn.x,
          custom_spawn.y,
          custom_spawn.z,
          custom_rot.yaw,
          custom_rot.pitch
      );
    } else {
      entity_manager->setDefaultSpawn(world->spawnPoint(), world->spawnRotation());
    }

    // 注册 /setspawn 回调：动态更新 World、EntityManager 默认出生点并持久化写入 spawn.json
    chat_handler->setSpawnCallback(
        [world, entity_manager, spawn_cfg_path](const Vector3d& pos, const Rotation& rot) {
          world->setSpawnPoint(pos);
          world->setSpawnRotation(rot);
          entity_manager->setDefaultSpawn(pos, rot);
          if (saveSpawnConfig(spawn_cfg_path, pos, rot)) {
            spdlog::info("[Main] Persisted custom spawn point to {}", spawn_cfg_path.string());
          } else {
            spdlog::error("[Main] Failed to persist custom spawn point to {}", spawn_cfg_path.string());
          }
        }
    );

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
