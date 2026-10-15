/**
 * @file ParkourMapConfig.cc
 * @brief 跑酷赛道伴生 INI 配置加载实现
 */

#include "game/parkour/ParkourMapConfig.h"

#include <format>

#include "common/IniConfig.h"

namespace mcpe::game::parkour {

std::optional<ParkourMapConfig>
ParkourMapConfig::loadFromIni(
    std::string_view map_name,
    const std::filesystem::path& ini_path
) {
  common::IniConfig ini;
  if (!ini.loadFromFile(ini_path)) {
    return std::nullopt;
  }

  ParkourMapConfig cfg;
  cfg.map_name = std::string(map_name);
  cfg.baked_file = ini.getString(
      "World",
      "baked_file",
      std::format("assets/maps/{}.mcpe_map", map_name)
  );
  cfg.spawn_point.x = ini.getDouble("World", "spawn_x", cfg.spawn_point.x);
  cfg.spawn_point.y = ini.getDouble("World", "spawn_y", cfg.spawn_point.y);
  cfg.spawn_point.z = ini.getDouble("World", "spawn_z", cfg.spawn_point.z);
  cfg.spawn_rotation.yaw =
      static_cast<float>(ini.getDouble("World", "spawn_yaw", cfg.spawn_rotation.yaw));
  cfg.spawn_rotation.pitch =
      static_cast<float>(ini.getDouble("World", "spawn_pitch", cfg.spawn_rotation.pitch));
  cfg.fall_reset_y = ini.getDouble("World", "fall_reset_y", cfg.fall_reset_y);

  if (auto start_aabb = ini.getAABB("Parkour", "start_min", "start_max")) {
    cfg.start_box = *start_aabb;
  }
  if (auto finish_aabb = ini.getAABB("Parkour", "finish_min", "finish_max")) {
    cfg.finish_box = *finish_aabb;
  }

  // 自动按序号扫描 checkpoint_1, checkpoint_2, ...
  for (int idx = 1; idx <= 32; ++idx) {
    const std::string min_k = std::format("checkpoint_{}_min", idx);
    const std::string max_k = std::format("checkpoint_{}_max", idx);
    auto cp_aabb = ini.getAABB("Parkour", min_k, max_k);
    if (!cp_aabb) {
      break;
    }

    ParkourCheckpoint cp;
    cp.box = *cp_aabb;
    const std::string spawn_k = std::format("checkpoint_{}_spawn", idx);
    if (auto sp = ini.getVector3d("Parkour", spawn_k)) {
      cp.respawn_pos = *sp;
    } else {
      cp.respawn_pos = Vector3d{
          (cp.box.min_pt.x + cp.box.max_pt.x) * 0.5,
          cp.box.min_pt.y,
          (cp.box.min_pt.z + cp.box.max_pt.z) * 0.5};
    }
    cp.respawn_rot.yaw = static_cast<float>(
        ini.getDouble("Parkour", std::format("checkpoint_{}_yaw", idx), 0.0)
    );
    cp.respawn_rot.pitch = static_cast<float>(
        ini.getDouble("Parkour", std::format("checkpoint_{}_pitch", idx), 0.0)
    );
    cfg.checkpoints.push_back(cp);
  }

  return cfg;
}

}  // namespace mcpe::game::parkour
