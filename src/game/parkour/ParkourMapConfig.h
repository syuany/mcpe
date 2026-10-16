/**
 * @file ParkourMapConfig.h
 * @brief 跑酷赛道伴生 INI 配置元数据与 AABB 碰撞盒定义（Game 玩法层 - parkour 子模块）
 */

#ifndef MCPE_GAME_PARKOUR_PARKOUR_MAP_CONFIG_H
#define MCPE_GAME_PARKOUR_PARKOUR_MAP_CONFIG_H

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "common/SpatialTypes.h"

namespace mcpe::game::parkour {

/**
 * @brief 跑酷中途检查点定义
 */
struct ParkourCheckpoint {
  AABB box{};
  Vector3d respawn_pos{};
  Rotation respawn_rot{};
};

/**
 * @brief 单张跑酷地图的完整配置项（对应 assets/maps/<map>.ini）
 */
struct ParkourMapConfig {
  std::string map_name{"FLASH"};
  std::filesystem::path baked_file{"assets/maps/parkour/FLASH.mcpe_map"};
  Vector3d spawn_point{1568.5, 37.0, -278.5};
  Rotation spawn_rotation{0.0f, 0.0f};
  double fall_reset_y{20.0};

  AABB start_box{Vector3d{1566.5, 37.0, -280.5}, Vector3d{1570.5, 40.0, -276.5}};
  std::vector<ParkourCheckpoint> checkpoints;
  AABB finish_box{Vector3d{0.0, 60.0, 0.0}, Vector3d{10.0, 70.0, 10.0}};

  /**
   * @brief 从指定 .ini 文件加载跑酷地图配置
   */
  [[nodiscard]] static std::optional<ParkourMapConfig> loadFromIni(
      std::string_view map_name,
      const std::filesystem::path& ini_path
  );

  /**
   * @brief 将当前赛道配置持久化写入对应伴生 .ini 文件
   */
  [[nodiscard]] bool saveToIni(const std::filesystem::path& ini_path) const;
};

}  // namespace mcpe::game::parkour

#endif  // MCPE_GAME_PARKOUR_PARKOUR_MAP_CONFIG_H
