#ifndef GAMETYPES_H
#define GAMETYPES_H

#include <cstdint>
#include <filesystem>
#include <string>

#include "common/IniConfig.h"

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
 * @brief 服务端运行配置与领域元数据（对接 src/settings.ini，对标 Cuberite cSettingsRepository）
 */
struct ServerConfig {
  uint16_t port{25566};
  int32_t protocol_version{340};
  std::string version_name{"1.12.2"};
  std::string motd{"§aA High Performance C++ Server §7[Asio/C++23]"};
  int32_t max_players{100};  // 全服总人数硬上限

  // 大厅分线与小游戏实例配额
  std::string default_lobby_map{"lobby"};
  int32_t lobby_max_players{50};     // 单个大厅分线人数上限
  int32_t max_lobby_instances{10};   // 大厅最大允许裂变分线数
  int32_t view_distance{5};          // 视距区块半径 (R=5 -> 11x11)

  std::string parkour_maps{"spiral"};
  int32_t parkour_max_players{25};   // 单个跑酷房间实例人数上限

  GameMode default_gamemode{GameMode::Adventure};
  Dimension default_dimension{Dimension::Overworld};
  Difficulty default_difficulty{Difficulty::Peaceful};
  std::string level_type{"flat"};
  bool reduced_debug_info{false};

  static ServerConfig&
  mutableInstance() {
    static ServerConfig kInstance;
    return kInstance;
  }

  static const ServerConfig&
  defaultInstance() {
    return mutableInstance();
  }

  bool
  loadFromIni(const std::filesystem::path& path) {
    common::IniConfig ini;
    if (!ini.loadFromFile(path)) {
      return false;
    }
    port = static_cast<uint16_t>(ini.getInt("Server", "port", port));
    max_players = ini.getInt("Server", "max_players", max_players);
    motd = ini.getString("Server", "motd", motd);

    default_lobby_map = ini.getString("Lobby", "default_map", default_lobby_map);
    lobby_max_players = ini.getInt("Lobby", "max_players_per_instance", lobby_max_players);
    max_lobby_instances = ini.getInt("Lobby", "max_instances", max_lobby_instances);
    view_distance = ini.getInt("Lobby", "view_distance", view_distance);

    parkour_maps = ini.getString("Parkour", "maps", parkour_maps);
    parkour_max_players = ini.getInt("Parkour", "max_players_per_instance", parkour_max_players);
    return true;
  }

  bool
  saveToIni(const std::filesystem::path& path) const {
    common::IniConfig ini;
    (void)ini.loadFromFile(path);
    ini.setInt("Server", "port", port);
    ini.setInt("Server", "max_players", max_players);
    ini.setString("Server", "motd", motd);

    ini.setString("Lobby", "default_map", default_lobby_map);
    ini.setInt("Lobby", "max_players_per_instance", lobby_max_players);
    ini.setInt("Lobby", "max_instances", max_lobby_instances);
    ini.setInt("Lobby", "view_distance", view_distance);

    ini.setString("Parkour", "maps", parkour_maps);
    ini.setInt("Parkour", "max_players_per_instance", parkour_max_players);
    return ini.saveToFile(path);
  }
};

}  // namespace mcpe::protocol

#endif  // GAMETYPES_H
