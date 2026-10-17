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
  cfg.companion_ini_path = ini_path;
  const std::string default_baked =
      std::filesystem::exists(std::format("assets/maps/parkour/{}.mcpe_map", map_name))
          ? std::format("assets/maps/parkour/{}.mcpe_map", map_name)
          : (std::filesystem::exists(std::format("assets/maps/baked/{}.mcpe_map", map_name))
                 ? std::format("assets/maps/baked/{}.mcpe_map", map_name)
                 : std::format("assets/maps/{}.mcpe_map", map_name));
  cfg.baked_file = ini.getString("World", "baked_file", default_baked);
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

bool
ParkourMapConfig::saveToIni(const std::filesystem::path& ini_path) const {
  common::IniConfig ini;
  (void)ini.loadFromFile(ini_path);

  ini.setString("World", "baked_file", baked_file.generic_string());
  ini.setDouble("World", "spawn_x", spawn_point.x);
  ini.setDouble("World", "spawn_y", spawn_point.y);
  ini.setDouble("World", "spawn_z", spawn_point.z);
  ini.setDouble("World", "spawn_yaw", spawn_rotation.yaw);
  ini.setDouble("World", "spawn_pitch", spawn_rotation.pitch);
  ini.setDouble("World", "fall_reset_y", fall_reset_y);

  ini.setAABB("Parkour", "start_min", "start_max", start_box);
  ini.setAABB("Parkour", "finish_min", "finish_max", finish_box);

  for (size_t i = 0; i < checkpoints.size(); ++i) {
    const size_t idx = i + 1;
    ini.setAABB(
        "Parkour",
        std::format("checkpoint_{}_min", idx),
        std::format("checkpoint_{}_max", idx),
        checkpoints[i].box
    );
    ini.setVector3d(
        "Parkour",
        std::format("checkpoint_{}_spawn", idx),
        checkpoints[i].respawn_pos
    );
    ini.setDouble(
        "Parkour",
        std::format("checkpoint_{}_yaw", idx),
        checkpoints[i].respawn_rot.yaw
    );
    ini.setDouble(
        "Parkour",
        std::format("checkpoint_{}_pitch", idx),
        checkpoints[i].respawn_rot.pitch
    );
  }

  return ini.saveToFile(ini_path);
}

}  // namespace mcpe::game::parkour
