#include "protocol/PlayHandler.h"

#include <cmath>
#include <spdlog/spdlog.h>

#include "network/PacketWriter.h"
#include "protocol/ChunkSerializer.h"
#include "protocol/ClientSession.h"
#include "protocol/GameTypes.h"
#include "protocol/PacketTypes.h"
#include "world/Chunk.h"
#include "world/EventQueue.h"
#include "world/FlatWorldGenerator.h"
#include "world/World.h"

namespace mcpe::protocol {

namespace {
std::shared_ptr<world::World> g_world = nullptr;
}

void
PlayHandler::setWorld(std::shared_ptr<world::World> world) {
  g_world = std::move(world);
}

std::shared_ptr<world::World>
PlayHandler::getWorld() {
  return g_world;
}

void
PlayHandler::onPlayerJoin(const std::shared_ptr<ClientSession>& session) {
  sendJoinGame(session, session->entityId());

  Vector3d spawn_pos{0.5, 4.0, 0.5};

  if (g_world) {
    spawn_pos = g_world->spawnPoint();
    const world::ChunkPos center_chunk = spawn_pos.toBlockPos().toChunkPos();
    constexpr int32_t kViewRadius = 3;  // 7x7 视距区块窗口

    for (int32_t cz = center_chunk.z - kViewRadius; cz <= center_chunk.z + kViewRadius; ++cz) {
      for (int32_t cx = center_chunk.x - kViewRadius; cx <= center_chunk.x + kViewRadius; ++cx) {
        const world::ChunkPos cpos{cx, cz};
        if (const auto* chunk = g_world->getChunk(cpos)) {
          ChunkSerializer::sendChunkData(session, *chunk, true);
        } else {
          // 下发空区块保持视野平滑
          world::Chunk empty_chunk(cpos);
          ChunkSerializer::sendChunkData(session, empty_chunk, true);
        }
      }
    }
  } else {
    // 兼容回退：未配置 World 时下发 5x5 超平坦区块
    world::FlatWorldGenerator generator;
    for (int cx = -2; cx <= 2; ++cx) {
      for (int cz = -2; cz <= 2; ++cz) {
        world::Chunk chunk(world::ChunkPos{cx, cz});
        generator.generate(chunk);
        ChunkSerializer::sendChunkData(session, chunk, true);
      }
    }
  }

  const int32_t teleport_id = session->nextTeleportId();
  sendPlayerPositionAndLook(session, spawn_pos.x, spawn_pos.y, spawn_pos.z, 0.0f, 0.0f, 0x00, teleport_id);
  session->startKeepAliveTimer();

  // 投递玩家进服事件至世界任务队列（触发同屏实体生成与 Tab 列表更新）
  if (auto q = session->eventQueue()) {
    world::WorldEvent ev;
    ev.session_id = session->sessionId();
    ev.entity_id = session->entityId();
    world::PlayerJoinEvent join;
    join.setName(session->username());
    join.uuid = session->uuidBytes();
    ev.payload = std::move(join);
    q->push(std::move(ev));
  }
}

void
PlayHandler::sendJoinGame(
    const std::shared_ptr<ClientSession>& session,
    int32_t entity_id,
    const ServerConfig& config
) {
  network::PacketWriter writer(session, 0x23);
  writer.writeBe<int32_t>(entity_id);
  writer.writeBe<uint8_t>(static_cast<uint8_t>(config.default_gamemode));
  writer.writeBe<int32_t>(static_cast<int32_t>(config.default_dimension));
  writer.writeBe<uint8_t>(static_cast<uint8_t>(config.default_difficulty));
  writer.writeBe<uint8_t>(static_cast<uint8_t>(config.max_players));
  writer.appendString(config.level_type);
  writer.writeBe<uint8_t>(config.reduced_debug_info ? 1 : 0);

  spdlog::info("[PlayHandler] Sent Join Game (0x23) to {}", session->remoteAddress());
}

void
PlayHandler::sendPlayerPositionAndLook(
    const std::shared_ptr<ClientSession>& session,
    double x,
    double y,
    double z,
    float yaw,
    float pitch,
    uint8_t flags,
    int32_t teleport_id
) {
  network::PacketWriter writer(session, 0x2F);
  writer.writeBe<double>(x);
  writer.writeBe<double>(y);
  writer.writeBe<double>(z);
  writer.writeBe<float>(yaw);
  writer.writeBe<float>(pitch);
  writer.writeBe<uint8_t>(flags);
  writer.appendVarInt(teleport_id);

  spdlog::info(
      "[PlayHandler] Sent Player Position And Look (0x2F) to {}, TeleportID: {}",
      session->remoteAddress(),
      teleport_id
  );
}

void
PlayHandler::handleTeleportConfirm(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  auto id_res = payload.readVarInt();
  if (!id_res) {
    spdlog::warn("[PlayHandler] Malformed Teleport Confirm from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int32_t confirmed_id = *id_res;
  if (confirmed_id == session->pendingTeleportId()) {
    session->clearPendingTeleportId();
    spdlog::info(
        "[PlayHandler] Teleport confirmed by {} (ID: {})",
        session->remoteAddress(),
        confirmed_id
    );
  } else {
    spdlog::warn(
        "[PlayHandler] Teleport ID mismatch from {}: expected {}, got {}",
        session->remoteAddress(),
        session->pendingTeleportId(),
        confirmed_id
    );
  }
}

void
PlayHandler::handleKeepAlive(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload) {
  // ⚠️ 1.12.2 铁律：Keep Alive 载荷必须是 8 字节大端整数 readBe<int64_t>()
  auto id_res = payload.readBe<int64_t>();
  if (!id_res) {
    spdlog::warn("[PlayHandler] Malformed Keep Alive echo from {}", session->remoteAddress());
    session->stop();
    return;
  }

  const int64_t keep_alive_id = *id_res;
  session->onKeepAliveResponse(keep_alive_id);
}

void
PlayHandler::handlePlayerMovement(
    const std::shared_ptr<ClientSession>& session,
    int32_t packet_id,
    ByteBuffer& payload
) {
  world::PlayerMoveEvent move_event;

  switch (packet_id) {
    case ServerboundPacket::Player: {
      if (payload.readableBytes() < 1) {
        payload.retrieveAll();
        return;
      }
      auto on_ground = payload.readBe<uint8_t>();
      move_event.on_ground = (*on_ground != 0);
      move_event.has_position = false;
      move_event.has_rotation = false;
      break;
    }
    case ServerboundPacket::PlayerPosition: {
      if (payload.readableBytes() < 25) {
        payload.retrieveAll();
        return;
      }
      auto x = payload.readBe<double>();
      auto y = payload.readBe<double>();
      auto z = payload.readBe<double>();
      auto on_ground = payload.readBe<uint8_t>();
      if (!std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z)) {
        spdlog::warn(
            "[PlayHandler] Non-finite position coordinates from {}",
            session->remoteAddress()
        );
        payload.retrieveAll();
        return;
      }
      move_event.position = Vector3d{*x, *y, *z};
      move_event.on_ground = (*on_ground != 0);
      move_event.has_position = true;
      move_event.has_rotation = false;
      break;
    }
    case ServerboundPacket::PlayerPositionAndLook: {
      if (payload.readableBytes() < 33) {
        payload.retrieveAll();
        return;
      }
      auto x = payload.readBe<double>();
      auto y = payload.readBe<double>();
      auto z = payload.readBe<double>();
      auto yaw = payload.readBe<float>();
      auto pitch = payload.readBe<float>();
      auto on_ground = payload.readBe<uint8_t>();
      if (!std::isfinite(*x) || !std::isfinite(*y) || !std::isfinite(*z) || !std::isfinite(*yaw) ||
          !std::isfinite(*pitch)) {
        spdlog::warn("[PlayHandler] Non-finite position/look from {}", session->remoteAddress());
        payload.retrieveAll();
        return;
      }
      move_event.position = Vector3d{*x, *y, *z};
      move_event.rotation = Rotation{*yaw, *pitch};
      move_event.on_ground = (*on_ground != 0);
      move_event.has_position = true;
      move_event.has_rotation = true;
      break;
    }
    case ServerboundPacket::PlayerLook: {
      if (payload.readableBytes() < 9) {
        payload.retrieveAll();
        return;
      }
      auto yaw = payload.readBe<float>();
      auto pitch = payload.readBe<float>();
      auto on_ground = payload.readBe<uint8_t>();
      if (!std::isfinite(*yaw) || !std::isfinite(*pitch)) {
        spdlog::warn("[PlayHandler] Non-finite rotation from {}", session->remoteAddress());
        payload.retrieveAll();
        return;
      }
      move_event.rotation = Rotation{*yaw, *pitch};
      move_event.on_ground = (*on_ground != 0);
      move_event.has_position = false;
      move_event.has_rotation = true;
      break;
    }
    default:
      payload.retrieveAll();
      return;
  }

  payload.retrieveAll();

  // 商业大厅虚空跌落复位 (Fall Reset: Y < 20.0 瞬移拉回出生点)
  if (move_event.has_position && move_event.position.y < 20.0) {
    const Vector3d reset_pos = g_world ? g_world->spawnPoint() : Vector3d{0.5, 4.0, 0.5};
    const int32_t tid = session->nextTeleportId();
    sendPlayerPositionAndLook(
        session,
        reset_pos.x,
        reset_pos.y,
        reset_pos.z,
        0.0f,
        0.0f,
        0x00,
        tid
    );
    spdlog::info(
        "[PlayHandler] Fall reset triggered for {} at Y={:.2f}",
        session->remoteAddress(),
        move_event.position.y
    );
    return;
  }

  if (auto q = session->eventQueue()) {
    world::WorldEvent ev;
    ev.session_id = session->sessionId();
    ev.entity_id = session->entityId();
    ev.payload = std::move(move_event);
    q->push(std::move(ev));
  }
}

void
PlayHandler::handleChatMessage(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  auto str_res = payload.readString(256);
  if (!str_res) {
    spdlog::warn("[PlayHandler] Malformed chat string from {}", session->remoteAddress());
    payload.retrieveAll();
    return;
  }

  if (auto q = session->eventQueue()) {
    world::WorldEvent ev;
    ev.session_id = session->sessionId();
    ev.entity_id = session->entityId();
    ev.payload = world::PlayerChatEvent{.message = *str_res};
    q->push(std::move(ev));
  }
}

}  // namespace mcpe::protocol
