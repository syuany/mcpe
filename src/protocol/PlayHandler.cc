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

namespace mcpe::protocol {

void
PlayHandler::onPlayerJoin(const std::shared_ptr<ClientSession>& session) {
  sendJoinGame(session, session->entityId());

  world::FlatWorldGenerator generator;
  for (int cx = -2; cx <= 2; ++cx) {
    for (int cz = -2; cz <= 2; ++cz) {
      world::Chunk chunk(world::ChunkPos{cx, cz});
      generator.generate(chunk);
      ChunkSerializer::sendChunkData(session, chunk, true);
    }
  }
  const int32_t teleport_id = session->nextTeleportId();
  sendPlayerPositionAndLook(session, 0.5, 4.0, 0.5, 0.0f, 0.0f, 0x00, teleport_id);
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
