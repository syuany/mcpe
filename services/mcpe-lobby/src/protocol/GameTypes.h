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
  int32_t keep_alive_timeout_seconds{45};  // 客户端心跳超时踢出阈值 (秒)

  // 大厅分线配额
  std::string default_lobby_map{"lobby"};
  int32_t lobby_max_players{50};     // 单个大厅分线人数上限
  int32_t max_lobby_instances{10};   // 大厅最大允许裂变分线数
  int32_t view_distance{5};          // 视距区块半径 (R=5 -> 11x11)

  GameMode default_gamemode{GameMode::Adventure};
  Dimension default_dimension{Dimension::Overworld};
  Difficulty default_difficulty{Difficulty::Peaceful};
  std::string level_type{"flat"};
  bool reduced_debug_info{false};
  
  // 权限与安全配置 [Security]
  bool dev_mode_enabled{true};
  std::string admins{"Notch, Developer_A, 5627dd98-e6be-3c21-b8a8-e92344183641"};
  std::string dev_secret{"mcpe_secret_2026"};

  // 集群网关与穿透配置 [Gateway]
  bool bungee_forwarding{true};
  std::string bungee_guard_token{""};

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
    keep_alive_timeout_seconds =
        ini.getInt("Server", "keep_alive_timeout_seconds", keep_alive_timeout_seconds);

    default_lobby_map = ini.getString("Lobby", "default_map", default_lobby_map);
    lobby_max_players = ini.getInt("Lobby", "max_players_per_instance", lobby_max_players);
    max_lobby_instances = ini.getInt("Lobby", "max_instances", max_lobby_instances);
    view_distance = ini.getInt("Lobby", "view_distance", view_distance);

    dev_mode_enabled = ini.getBool("Security", "dev_mode_enabled", dev_mode_enabled);
    admins = ini.getString("Security", "admins", admins);
    dev_secret = ini.getString("Security", "dev_secret", dev_secret);

    bungee_forwarding = ini.getBool("Gateway", "bungee_forwarding", bungee_forwarding);
    bungee_guard_token = ini.getString("Gateway", "bungee_guard_token", bungee_guard_token);
    return true;
  }

  bool
  saveToIni(const std::filesystem::path& path) const {
    common::IniConfig ini;
    (void)ini.loadFromFile(path);
    ini.setInt("Server", "port", port);
    ini.setInt("Server", "max_players", max_players);
    ini.setString("Server", "motd", motd);
    ini.setInt("Server", "keep_alive_timeout_seconds", keep_alive_timeout_seconds);

    ini.setString("Lobby", "default_map", default_lobby_map);
    ini.setInt("Lobby", "max_players_per_instance", lobby_max_players);
    ini.setInt("Lobby", "max_instances", max_lobby_instances);
    ini.setInt("Lobby", "view_distance", view_distance);

    ini.setBool("Security", "dev_mode_enabled", dev_mode_enabled);
    ini.setString("Security", "admins", admins);
    ini.setString("Security", "dev_secret", dev_secret);

    ini.setBool("Gateway", "bungee_forwarding", bungee_forwarding);
    ini.setString("Gateway", "bungee_guard_token", bungee_guard_token);
    return ini.saveToFile(path);
  }
};

}  // namespace mcpe::protocol

#endif  // GAMETYPES_H
