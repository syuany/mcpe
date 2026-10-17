#include "protocol/PlayHandler.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <utility>

#include "game/InstanceManager.h"
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
std::shared_ptr<game::InstanceManager> g_instance_manager = nullptr;

void
streamWorldChunksAndEnvironment(
    const std::shared_ptr<ClientSession>& session,
    const world::World& target_world
) {
  const Vector3d spawn_pos = target_world.spawnPoint();
  const Rotation spawn_rot = target_world.spawnRotation();
  const world::ChunkPos center_chunk = spawn_pos.toBlockPos().toChunkPos();
  const int32_t view_radius = ServerConfig::defaultInstance().view_distance;

  for (int32_t cz = center_chunk.z - view_radius; cz <= center_chunk.z + view_radius; ++cz) {
    for (int32_t cx = center_chunk.x - view_radius; cx <= center_chunk.x + view_radius; ++cx) {
      const world::ChunkPos cpos{cx, cz};
      if (const auto* chunk = target_world.getChunk(cpos)) {
        ChunkSerializer::sendChunkData(session, *chunk, true);
      } else {
        world::Chunk empty_chunk(cpos);
        ChunkSerializer::sendChunkData(session, empty_chunk, true);
      }
    }
  }

  // 0x46 Spawn Position
  network::PacketWriter spawn_pos_writer(session, ClientboundPacket::SpawnPosition);
  spawn_pos_writer.writeBe<uint64_t>(spawn_pos.toBlockPos().toProtocolPosition());

  // 0x2C Player Abilities: 默认无敌且关闭飞行
  session->setCanFly(false);
  session->setIsFlying(false);
  PlayHandler::sendPlayerAbilities(session, PlayerAbilitiesFlags::Invulnerable);

  // 0x47 Time Update: 永久锁定正午太阳
  PlayHandler::sendTimeUpdate(session, 6000LL, -6000LL);

  const int32_t teleport_id = session->nextTeleportId();
  session->setPosition(spawn_pos);
  session->setRotation(spawn_rot);
  PlayHandler::sendPlayerPositionAndLook(
      session,
      spawn_pos.x,
      spawn_pos.y,
      spawn_pos.z,
      spawn_rot.yaw,
      spawn_rot.pitch,
      0x00,
      teleport_id
  );
}
}  // namespace

void
PlayHandler::setWorld(std::shared_ptr<world::World> world) {
  g_world = std::move(world);
}

std::shared_ptr<world::World>
PlayHandler::getWorld() {
  return g_world;
}

void
PlayHandler::setInstanceManager(std::shared_ptr<game::InstanceManager> mgr) {
  g_instance_manager = std::move(mgr);
}

std::shared_ptr<game::InstanceManager>
PlayHandler::getInstanceManager() {
  return g_instance_manager;
}

void
PlayHandler::onPlayerJoin(const std::shared_ptr<ClientSession>& session) {
  sendJoinGame(session, session->entityId());

  // 若启用了多实例调度中心，自动分配未满员的大厅分线 (lobby-1, lobby-2, ...)
  if (g_instance_manager) {
    auto lobby = g_instance_manager->acquireLobbyForPlayer(session->sessionId());
    if (lobby) {
      session->setCurrentInstanceId(lobby->instanceId());
      session->setEventQueue(lobby->eventQueuePtr());
      streamWorldChunksAndEnvironment(session, lobby->world());
      session->startKeepAliveTimer();

      (void)g_instance_manager->transferPlayer(
          session->sessionId(),
          session->entityId(),
          session->username(),
          session->uuidBytes(),
          lobby
      );
      return;
    }
  }

  if (g_world) {
    streamWorldChunksAndEnvironment(session, *g_world);
  } else {
    Vector3d spawn_pos{0.5, 4.0, 0.5};
    Rotation spawn_rot{0.0f, 0.0f};
    world::FlatWorldGenerator generator;
    for (int cx = -2; cx <= 2; ++cx) {
      for (int cz = -2; cz <= 2; ++cz) {
        world::Chunk chunk(world::ChunkPos{cx, cz});
        generator.generate(chunk);
        ChunkSerializer::sendChunkData(session, chunk, true);
      }
    }
    network::PacketWriter spawn_pos_writer(session, ClientboundPacket::SpawnPosition);
    spawn_pos_writer.writeBe<uint64_t>(spawn_pos.toBlockPos().toProtocolPosition());
    sendPlayerAbilities(session, PlayerAbilitiesFlags::Invulnerable);
    sendTimeUpdate(session, 6000LL, -6000LL);

    const int32_t teleport_id = session->nextTeleportId();
    session->setPosition(spawn_pos);
    session->setRotation(spawn_rot);
    sendPlayerPositionAndLook(
        session,
        spawn_pos.x,
        spawn_pos.y,
        spawn_pos.z,
        spawn_rot.yaw,
        spawn_rot.pitch,
        0x00,
        teleport_id
    );
  }

  session->startKeepAliveTimer();

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
  network::PacketWriter writer(session, ClientboundPacket::JoinGame);
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
PlayHandler::sendRespawn(
    const std::shared_ptr<ClientSession>& session,
    Dimension dimension,
    Difficulty difficulty,
    GameMode gamemode,
    std::string_view level_type
) {
  network::PacketWriter writer(session, ClientboundPacket::Respawn);
  writer.writeBe<int32_t>(static_cast<int32_t>(dimension));
  writer.writeBe<uint8_t>(static_cast<uint8_t>(difficulty));
  writer.writeBe<uint8_t>(static_cast<uint8_t>(gamemode));
  writer.appendString(level_type);

  spdlog::debug(
      "[PlayHandler] Sent Respawn (0x35) to {}, dim={}",
      session->remoteAddress(),
      static_cast<int32_t>(dimension)
  );
}

void
PlayHandler::switchPlayerWorld(
    const std::shared_ptr<ClientSession>& session,
    const world::World& target_world,
    GameMode target_gamemode
) {
  // 1. 先发送过渡维度 Respawn (-1 Nether) 再发目标维度 Respawn (0 Overworld)，
  //    强制原版 1.12.2 客户端彻底清空同维度旧世界的区块渲染缓存与残留实体
  sendRespawn(session, Dimension::Nether, Difficulty::Peaceful, target_gamemode, "flat");
  sendRespawn(session, Dimension::Overworld, Difficulty::Peaceful, target_gamemode, "flat");

  // 2. 流式下发目标世界区块与环境初始化包，并通过 0x2F 定位至新世界起点
  streamWorldChunksAndEnvironment(session, target_world);

  spdlog::info("[PlayHandler] Switched world for {}", session->remoteAddress());
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
  network::PacketWriter writer(session, ClientboundPacket::PlayerPositionAndLook);
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

ByteBuffer
PlayHandler::buildEntityEffect(
    int32_t entity_id,
    uint8_t effect_id,
    uint8_t amplifier,
    int32_t duration_ticks,
    uint8_t flags
) {
  network::PacketBuilder builder(ClientboundPacket::EntityEffect);
  builder.appendVarInt(entity_id);
  builder.appendByte(effect_id);
  builder.appendByte(amplifier);
  builder.appendVarInt(duration_ticks);
  builder.appendByte(flags);
  return builder.build();
}

ByteBuffer
PlayHandler::buildRemoveEntityEffect(
    int32_t entity_id,
    uint8_t effect_id
) {
  network::PacketBuilder builder(ClientboundPacket::RemoveEntityEffect);
  builder.appendVarInt(entity_id);
  builder.appendByte(effect_id);
  return builder.build();
}

ByteBuffer
PlayHandler::buildEntityVelocity(
    int32_t entity_id,
    int16_t velocity_x,
    int16_t velocity_y,
    int16_t velocity_z
) {
  network::PacketBuilder builder(ClientboundPacket::EntityVelocity);
  builder.appendVarInt(entity_id);
  builder.writeBe<int16_t>(velocity_x);
  builder.writeBe<int16_t>(velocity_y);
  builder.writeBe<int16_t>(velocity_z);
  return builder.build();
}

void
PlayHandler::sendEntityEffect(
    const std::shared_ptr<ClientSession>& session,
    int32_t entity_id,
    uint8_t effect_id,
    uint8_t amplifier,
    int32_t duration_ticks,
    uint8_t flags
) {
  if (!session) return;
  session->sendPacket(buildEntityEffect(entity_id, effect_id, amplifier, duration_ticks, flags));
}

void
PlayHandler::sendRemoveEntityEffect(
    const std::shared_ptr<ClientSession>& session,
    int32_t entity_id,
    uint8_t effect_id
) {
  if (!session) return;
  session->sendPacket(buildRemoveEntityEffect(entity_id, effect_id));
}

void
PlayHandler::sendEntityVelocity(
    const std::shared_ptr<ClientSession>& session,
    int32_t entity_id,
    int16_t velocity_x,
    int16_t velocity_y,
    int16_t velocity_z
) {
  if (!session) return;
  session->sendPacket(buildEntityVelocity(entity_id, velocity_x, velocity_y, velocity_z));
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

  // 当未启用多实例管理器（单世界兼容模式）时，在 PlayHandler 中执行大厅虚空拉回；
  // 启用 InstanceManager 后，由各实例专属的 20 TPS 物理线程钩子 (LobbyInstance / ParkourInstance) 精确判定。
  if (!g_instance_manager) {
    const double fall_threshold = session->canFly() ? 0.0 : 20.0;
    if (move_event.has_position && move_event.position.y < fall_threshold) {
      const Vector3d reset_pos = g_world ? g_world->spawnPoint() : Vector3d{0.5, 4.0, 0.5};
      const Rotation reset_rot = g_world ? g_world->spawnRotation() : Rotation{0.0f, 0.0f};
      const int32_t tid = session->nextTeleportId();
      sendPlayerPositionAndLook(
          session,
          reset_pos.x,
          reset_pos.y,
          reset_pos.z,
          reset_rot.yaw,
          reset_rot.pitch,
          0x00,
          tid
      );
      move_event.position = reset_pos;
      move_event.rotation = reset_rot;
      move_event.has_rotation = true;
      move_event.on_ground = true;
    }
  }

  if (move_event.has_position) {
    session->setPosition(move_event.position);
  }
  if (move_event.has_rotation) {
    session->setRotation(move_event.rotation);
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
