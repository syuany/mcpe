/**
 * @file EntityManager.cc
 * @brief 实体管理器与位移广播实现
 */

#include "world/EntityManager.h"

#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>
#include <utility>

namespace mcpe::world {

EntityManager::EntityManager(
    PacketSender sender,
    BroadcastExceptSender broadcast_except,
    BroadcastSender broadcast
)
    : sender_(std::move(sender)),
      broadcast_except_(std::move(broadcast_except)),
      broadcast_(std::move(broadcast)) {}

void
EntityManager::setSenders(
    PacketSender sender,
    BroadcastExceptSender broadcast_except,
    BroadcastSender broadcast
) {
  sender_ = std::move(sender);
  broadcast_except_ = std::move(broadcast_except);
  broadcast_ = std::move(broadcast);
}

void
EntityManager::addPlayer(
    uint64_t session_id,
    int32_t entity_id,
    std::string_view username,
    const std::array<uint8_t, 16>& uuid,
    const Vector3d& spawn_pos,
    const Rotation& spawn_rot
) {
  PlayerEntity new_player;
  new_player.session_id = session_id;
  new_player.entity_id = entity_id;
  new_player.username = std::string(username);
  new_player.uuid = uuid;
  new_player.position = spawn_pos;
  new_player.rotation = spawn_rot;
  new_player.last_synced_pos = spawn_pos;
  new_player.last_synced_rot = spawn_rot;
  new_player.dirty = false;

  spdlog::info(
      "[EntityManager] Adding player '{}' (EID: {}, SID: {}) to world",
      new_player.username,
      entity_id,
      session_id
  );

  // 1. 先向既有玩家下发新玩家的 Player List Item (0x2E, Action=0) 与 Spawn Player (0x05)
  auto new_list_pkt =
      buildPlayerListItemPacket(new_player, protocol::PlayerListItemAction::AddPlayer);
  auto new_spawn_pkt = buildSpawnPlayerPacket(new_player);

  for (const auto& [other_sid, other_player] : players_) {
    if (sender_) {
      sender_(other_sid, new_list_pkt.clone());
      sender_(other_sid, new_spawn_pkt.clone());

      // 2. 向新玩家展示既有玩家的 0x2E 与 0x05
      sender_(
          session_id,
          buildPlayerListItemPacket(other_player, protocol::PlayerListItemAction::AddPlayer)
      );
      sender_(session_id, buildSpawnPlayerPacket(other_player));
    }
  }

  // 3. 向新玩家自己也下发一份自己的 Player List Item (0x2E)，使其自身在 Tab 列表中正常可见
  if (sender_) {
    sender_(session_id, std::move(new_list_pkt));
  }

  players_[session_id] = std::move(new_player);
}

void
EntityManager::removePlayer(uint64_t session_id) {
  auto it = players_.find(session_id);
  if (it == players_.end()) {
    return;
  }

  const auto& player = it->second;
  spdlog::info(
      "[EntityManager] Removing player '{}' (EID: {}, SID: {}) from world",
      player.username,
      player.entity_id,
      session_id
  );

  // 广播 0x32 Destroy Entities 销毁模型
  auto destroy_pkt = buildDestroyEntitiesPacket(player.entity_id);
  // 广播 0x2E Player List Item (Action=4 Remove Player) 移除 Tab 列表项
  auto remove_list_pkt =
      buildPlayerListItemPacket(player, protocol::PlayerListItemAction::RemovePlayer);

  if (broadcast_except_) {
    broadcast_except_(session_id, std::move(destroy_pkt));
    broadcast_except_(session_id, std::move(remove_list_pkt));
  } else if (broadcast_) {
    broadcast_(std::move(destroy_pkt));
    broadcast_(std::move(remove_list_pkt));
  }

  players_.erase(it);
}

void
EntityManager::updatePlayerPosition(
    uint64_t session_id,
    const std::optional<Vector3d>& pos,
    const std::optional<Rotation>& rot,
    bool on_ground
) {
  auto it = players_.find(session_id);
  if (it == players_.end()) {
    return;
  }

  auto& player = it->second;
  if (pos.has_value()) {
    player.position = *pos;
  }
  if (rot.has_value()) {
    player.rotation = *rot;
  }
  player.on_ground = on_ground;
  player.dirty = true;
}

void
EntityManager::updateAndBroadcast() {
  for (auto& [sid, p] : players_) {
    if (!p.dirty) {
      continue;
    }

    const Vector3d delta = p.position - p.last_synced_pos;

    // 相对位移定点数运算（以 1/4096 方块为单位）
    const auto dx = static_cast<int16_t>(std::round(delta.x * 4096.0));
    const auto dy = static_cast<int16_t>(std::round(delta.y * 4096.0));
    const auto dz = static_cast<int16_t>(std::round(delta.z * 4096.0));

    const bool move_changed = (dx != 0 || dy != 0 || dz != 0);

    const uint8_t cur_yaw = p.rotation.toByteYaw();
    const uint8_t cur_pitch = p.rotation.toBytePitch();
    const uint8_t last_yaw = p.last_synced_rot.toByteYaw();
    const uint8_t last_pitch = p.last_synced_rot.toBytePitch();

    const bool look_changed = (cur_yaw != last_yaw || cur_pitch != last_pitch);

    if (!move_changed && !look_changed) {
      p.dirty = false;
      continue;
    }

    // 8.0 格临界溢出检测：如果单轴位移绝对值 >= 8.0，降级下发 0x4C Entity Teleport
    const bool overflow =
        (std::abs(delta.x) >= 8.0 || std::abs(delta.y) >= 8.0 || std::abs(delta.z) >= 8.0);

    ByteBuffer pkt;
    if (overflow) {
      pkt = buildEntityTeleportPacket(p);
    } else if (move_changed && look_changed) {
      pkt = buildLookAndRelMovePacket(p.entity_id, delta, p.rotation, p.on_ground);
    } else if (move_changed) {
      pkt = buildRelMovePacket(p.entity_id, delta, p.on_ground);
    } else {
      pkt = buildEntityLookPacket(p.entity_id, p.rotation, p.on_ground);
    }

    if (broadcast_except_) {
      broadcast_except_(sid, std::move(pkt));
    }

    p.last_synced_pos = p.position;
    p.last_synced_rot = p.rotation;
    p.dirty = false;
  }
}

std::string
EntityManager::getPlayerName(uint64_t session_id) const {
  auto it = players_.find(session_id);
  if (it != players_.end()) {
    return it->second.username;
  }
  return "Unknown";
}

bool
EntityManager::hasPlayer(uint64_t session_id) const {
  return players_.contains(session_id);
}

const PlayerEntity*
EntityManager::getPlayer(uint64_t session_id) const {
  auto it = players_.find(session_id);
  if (it != players_.end()) {
    return &it->second;
  }
  return nullptr;
}

ByteBuffer
EntityManager::buildPlayerListItemPacket(
    const PlayerEntity& player,
    protocol::PlayerListItemAction action
) {
  ByteBuffer buf;
  buf.appendVarInt(protocol::ClientboundPacket::PlayerListItem);
  buf.appendVarInt(static_cast<int32_t>(action));
  buf.appendVarInt(1);  // 1 player entry

  if (action == protocol::PlayerListItemAction::AddPlayer) {
    buf.append(player.uuid.data(), 16);
    buf.appendString(player.username);
    buf.appendVarInt(0);  // 0 properties
    buf.appendVarInt(1);  // Gamemode: Creative
    buf.appendVarInt(5);  // Ping: 5ms
    buf.appendByte(0);    // Has Display Name: false
  } else if (action == protocol::PlayerListItemAction::RemovePlayer) {
    buf.append(player.uuid.data(), 16);
  }

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

ByteBuffer
EntityManager::buildSpawnPlayerPacket(const PlayerEntity& player) {
  ByteBuffer buf;
  buf.appendVarInt(protocol::ClientboundPacket::SpawnPlayer);
  buf.appendVarInt(player.entity_id);
  buf.append(player.uuid.data(), 16);
  buf.writeBe<double>(player.position.x);
  buf.writeBe<double>(player.position.y);
  buf.writeBe<double>(player.position.z);
  buf.appendByte(player.rotation.toByteYaw());
  buf.appendByte(player.rotation.toBytePitch());
  buf.appendByte(0xFF);  // Metadata terminator

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

ByteBuffer
EntityManager::buildRelMovePacket(int32_t entity_id, const Vector3d& delta, bool on_ground) {
  ByteBuffer buf;
  buf.appendVarInt(protocol::ClientboundPacket::EntityRelativeMove);
  buf.appendVarInt(entity_id);
  buf.writeBe<int16_t>(static_cast<int16_t>(std::round(delta.x * 4096.0)));
  buf.writeBe<int16_t>(static_cast<int16_t>(std::round(delta.y * 4096.0)));
  buf.writeBe<int16_t>(static_cast<int16_t>(std::round(delta.z * 4096.0)));
  buf.appendByte(on_ground ? 1 : 0);

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

ByteBuffer
EntityManager::buildLookAndRelMovePacket(
    int32_t entity_id,
    const Vector3d& delta,
    const Rotation& rot,
    bool on_ground
) {
  ByteBuffer buf;
  buf.appendVarInt(protocol::ClientboundPacket::EntityLookAndRelativeMove);
  buf.appendVarInt(entity_id);
  buf.writeBe<int16_t>(static_cast<int16_t>(std::round(delta.x * 4096.0)));
  buf.writeBe<int16_t>(static_cast<int16_t>(std::round(delta.y * 4096.0)));
  buf.writeBe<int16_t>(static_cast<int16_t>(std::round(delta.z * 4096.0)));
  buf.appendByte(rot.toByteYaw());
  buf.appendByte(rot.toBytePitch());
  buf.appendByte(on_ground ? 1 : 0);

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

ByteBuffer
EntityManager::buildEntityLookPacket(int32_t entity_id, const Rotation& rot, bool on_ground) {
  ByteBuffer buf;
  buf.appendVarInt(protocol::ClientboundPacket::EntityLook);
  buf.appendVarInt(entity_id);
  buf.appendByte(rot.toByteYaw());
  buf.appendByte(rot.toBytePitch());
  buf.appendByte(on_ground ? 1 : 0);

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

ByteBuffer
EntityManager::buildEntityTeleportPacket(const PlayerEntity& player) {
  ByteBuffer buf;
  buf.appendVarInt(protocol::ClientboundPacket::EntityTeleport);
  buf.appendVarInt(player.entity_id);
  buf.writeBe<double>(player.position.x);
  buf.writeBe<double>(player.position.y);
  buf.writeBe<double>(player.position.z);
  buf.appendByte(player.rotation.toByteYaw());
  buf.appendByte(player.rotation.toBytePitch());
  buf.appendByte(player.on_ground ? 1 : 0);

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

ByteBuffer
EntityManager::buildDestroyEntitiesPacket(int32_t entity_id) {
  ByteBuffer buf;
  buf.appendVarInt(protocol::ClientboundPacket::DestroyEntities);
  buf.appendVarInt(1);  // count = 1
  buf.appendVarInt(entity_id);

  buf.prependVarInt(static_cast<int32_t>(buf.readableBytes()));
  return buf;
}

}  // namespace mcpe::world
