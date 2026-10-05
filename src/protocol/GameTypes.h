#ifndef GAMETYPES_H
#define GAMETYPES_H

#include <cstdint>
#include <string>

namespace mcpe::protocol {

/**
 * @brief Minecraft 游戏模式枚举
 */
enum class GameMode : uint8_t { Survival = 0, Creative = 1, Adventure = 2, Spectator = 3 };

/**
 * @brief Minecraft 维度枚举
 */
enum class Dimension : int32_t { Nether = -1, Overworld = 0, End = 1 };

/**
 * @brief Minecraft 难度枚举
 */
enum class Difficulty : uint8_t { Peaceful = 0, Easy = 1, Normal = 2, Hard = 3 };

/**
 * @brief 服务端运行配置与领域元数据（消除硬编码魔法数字，对标 Cuberite cSettingsRepository）
 */
struct ServerConfig {
  int32_t protocol_version{340};
  std::string version_name{"1.12.2"};
  std::string motd{"§aA High Performance C++ Server §7[Asio/C++23]"};
  int32_t max_players{100};

  GameMode default_gamemode{GameMode::Creative};
  Dimension default_dimension{Dimension::Overworld};
  Difficulty default_difficulty{Difficulty::Normal};
  std::string level_type{"flat"};
  bool reduced_debug_info{false};

  static const ServerConfig&
  defaultInstance() {
    static const ServerConfig kDefault;
    return kDefault;
  }
};

}  // namespace mcpe::protocol

#endif  // GAMETYPES_H
