/**
 * @file LobbyInstance.cc
 * @brief 商业大厅分线实例实现
 */

#include "game/lobby/LobbyInstance.h"

#include <spdlog/spdlog.h>

#include <format>
#include <utility>

namespace mcpe::game::lobby {

LobbyInstance::LobbyInstance(
    std::string instance_id,
    std::shared_ptr<world::World> world,
    size_t max_players,
    double fall_reset_y,
    PacketUnicastFn unicast_fn
)
    : IGameInstance(
          std::move(instance_id),
          InstanceType::Lobby,
          std::move(world),
          max_players,
          std::move(unicast_fn)
      ),
      fall_reset_y_(fall_reset_y) {}

void
LobbyInstance::onPlayerJoined(uint64_t session_id, std::string_view username) {
  spdlog::info(
      "[LobbyInstance:{}] Player '{}' (SID {}) entered lobby shard ({}/{})",
      instanceId(),
      username,
      session_id,
      playerCount(),
      maxPlayers()
  );
}

void
LobbyInstance::onPlayerMoved(uint64_t session_id, world::PlayerMoveEvent& move_event) {
  if (!move_event.has_position) {
    return;
  }

  if (move_event.position.y < fall_reset_y_) {
    const Vector3d reset_pos = world_->spawnPoint();
    const Rotation reset_rot = world_->spawnRotation();
    if (teleport_fn_) {
      teleport_fn_(session_id, reset_pos, reset_rot);
    }
    move_event.position = reset_pos;
    move_event.rotation = reset_rot;
    move_event.has_rotation = true;
    move_event.on_ground = true;
  }
}

}  // namespace mcpe::game::lobby
