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
  Rotation spawn_rot{0.0f, 0.0f};

  if (g_world) {
    spawn_pos = g_world->spawnPoint();
    spawn_rot = g_world->spawnRotation();
    const world::ChunkPos center_chunk = spawn_pos.toBlockPos().toChunkPos();
    constexpr int32_t kViewRadius = 5;  // 11x11 视距区块窗口（覆盖大厅全貌）

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

  // 0x46 Spawn Position: 指南针与官方世界出生点坐标
  network::PacketWriter spawn_pos_writer(session, ClientboundPacket::SpawnPosition);
  spawn_pos_writer.writeBe<uint64_t>(spawn_pos.toBlockPos().toProtocolPosition());

  // 0x2C Player Abilities: 默认下发无敌与禁用飞行（行走模式）
  sendPlayerAbilities(session, PlayerAbilitiesFlags::Invulnerable);

  // 0x47 Time Update: 永久锁定正午太阳（世界年龄 6000，时间 -6000 冻结昼夜更替，保持大厅永昼晴朗）
  sendTimeUpdate(session, 6000LL, -6000LL);

  const int32_t teleport_id = session->nextTeleportId();
  session->setPosition(spawn_pos);
  session->setRotation(spawn_rot);
  sendPlayerPositionAndLook(session, spawn_pos.x, spawn_pos.y, spawn_pos.z, spawn_rot.yaw, spawn_rot.pitch, 0x00, teleport_id);
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
PlayHandler::sendPlayerAbilities(
    const std::shared_ptr<ClientSession>& session,
    uint8_t flags,
    float flying_speed,
    float fov_modifier
) {
  network::PacketWriter writer(session, ClientboundPacket::PlayerAbilities);
  writer.writeBe<uint8_t>(flags);
  writer.writeBe<float>(flying_speed);
  writer.writeBe<float>(fov_modifier);

  spdlog::debug(
      "[PlayHandler] Sent Player Abilities (0x2C) to {}, flags=0x{:02X}, fly_speed={:.2f}",
      session->remoteAddress(),
      flags,
      flying_speed
  );
}

void
PlayHandler::sendTimeUpdate(
    const std::shared_ptr<ClientSession>& session,
    int64_t world_age,
    int64_t time_of_day
) {
  network::PacketWriter writer(session, ClientboundPacket::TimeUpdate);
  writer.writeBe<int64_t>(world_age);
  writer.writeBe<int64_t>(time_of_day);

  spdlog::debug(
      "[PlayHandler] Sent Time Update (0x47) to {}, world_age={}, time_of_day={}",
      session->remoteAddress(),
      world_age,
      time_of_day
  );
}

void
PlayHandler::handlePlayerAbilities(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  if (payload.readableBytes() < 1) {
    payload.retrieveAll();
    return;
  }

  auto flags_opt = payload.readBe<uint8_t>();
  if (!flags_opt) {
    payload.retrieveAll();
    return;
  }

  const uint8_t flags = *flags_opt;
  const bool client_flying = (flags & PlayerAbilitiesFlags::Flying) != 0;

  if (client_flying && !session->canFly()) {
    // 客户端未获授权试图起飞，下发修正包强制拉回行走状态
    spdlog::warn(
        "[PlayHandler] Unauthorized flight packet from {}, resetting abilities",
        session->remoteAddress()
    );
    sendPlayerAbilities(session, PlayerAbilitiesFlags::Invulnerable);
  } else {
    session->setIsFlying(client_flying);
    spdlog::debug(
        "[PlayHandler] Updated flight state for {}: is_flying={}",
        session->remoteAddress(),
        client_flying
    );
  }

  payload.retrieveAll();
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

  if (move_event.has_position) {
    session->setPosition(move_event.position);
  }
  if (move_event.has_rotation) {
    session->setRotation(move_event.rotation);
  }

  // 商业大厅虚空跌落复位 (Fall Reset)
  // 正常行走跌入 Y < 20.0 触发复位；开启飞行模式的玩家在极端虚空 Y < 0.0 时触发复位，避免在岛屿底部漫游时误判
  const double fall_threshold = session->canFly() ? 0.0 : 20.0;
  if (move_event.has_position && move_event.position.y < fall_threshold) {
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
        "[PlayHandler] Fall reset triggered for {} at Y={:.2f} (can_fly={})",
        session->remoteAddress(),
        move_event.position.y,
        session->canFly()
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

void
PlayHandler::handlePlayerDigging(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  spdlog::debug(
      "[PlayHandler] Block digging rejected (lobby block protection) from {}",
      session->remoteAddress()
  );
  payload.retrieveAll();
}

void
PlayHandler::handleBlockPlacement(
    const std::shared_ptr<ClientSession>& session,
    ByteBuffer& payload
) {
  spdlog::debug(
      "[PlayHandler] Block placement rejected (lobby block protection) from {}",
      session->remoteAddress()
  );
  payload.retrieveAll();
}

}  // namespace mcpe::protocol
