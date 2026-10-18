/**
 * @file main.cc
 * @brief MCPE 离线地图烘焙器命令行工具 (tools/map_baker)
 *
 * 功能定位：
 * 自动扫描 assets/maps/lobby 目录下的所有支持源文件 (.zip/.rar/.schematic)，
 * 烘焙输出至各输入文件所在相同目录下同名 .mcpe_map 镜像文件，并在同级目录下生成/补全伴生 .ini 配置。
 *
 * 用法：
 *   ./bin/map_baker                        # 自动全量扫描 lobby 目录，烘焙源地图并生成伴生配置
 *   ./bin/map_baker [input_path]           # 烘焙指定文件至其同级目录下的 <stem>.mcpe_map
 *   ./bin/map_baker [input_path] [out_path]# 烘焙指定文件至指定输出路径
 */

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "AnvilLoader.h"
#include "SchematicLoader.h"
#include "common/IniConfig.h"
#include "world/World.h"

using namespace mcpe;
using namespace mcpe::world;

namespace {

std::filesystem::path
findLobbyDir() {
  for (const auto& candidate : {"assets/maps/lobby", "../assets/maps/lobby", "../../assets/maps/lobby"}) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return "assets/maps/lobby";
}

bool
bakeFromInput(const std::filesystem::path& input_path, World& world) {
  if (!std::filesystem::exists(input_path)) {
    const std::string in_str = input_path.string();
    spdlog::error("[MapBaker] Input path does not exist: {}", in_str);
    return false;
  }

  if (std::filesystem::is_directory(input_path)) {
    size_t chunks = AnvilLoader::loadFromDirectory(input_path, world);
    return chunks > 0;
  }

  const auto ext = input_path.extension().string();
  if (ext == ".zip") {
    size_t chunks = AnvilLoader::loadFromZip(input_path, world);
    return chunks > 0;
  }

  if (ext == ".rar") {
    size_t chunks = AnvilLoader::loadFromRar(input_path, world);
    return chunks > 0;
  }

  if (ext == ".schematic") {
    if (!SchematicLoader::loadIntoWorld(input_path, BlockPos{0, 0, 0}, world)) {
      return false;
    }
    // 浮空岛中心默认出生点并自适应安全地面高度
    world.setSpawnPoint(Vector3d{48.5, 34.0, 48.5});
    world.adjustSpawnPointToSafeGround();
    return world.chunkCount() > 0;
  }

  const std::string in_str = input_path.string();
  spdlog::error("[MapBaker] Unsupported input format: {}", in_str);
  return false;
}

bool
bakeAndSave(const std::filesystem::path& in_path, const std::filesystem::path& out_path) {
  World world;
  const std::string in_str = in_path.string();
  const std::string out_str = out_path.string();

  spdlog::info("[MapBaker] Baking: '{}' -> '{}'...", in_str, out_str);
  if (!bakeFromInput(in_path, world)) {
    spdlog::error("[MapBaker] Failed to parse and load: {}", in_str);
    return false;
  }

  std::error_code ec;
  std::filesystem::create_directories(out_path.parent_path(), ec);

  if (world.chunkCount() == 0) {
    spdlog::error("[MapBaker] World is empty, aborting bake for: {}", in_str);
    return false;
  }

  if (!world.saveBakedMap(out_path)) {
    spdlog::error("[MapBaker] Failed to write baked map to: {}", out_str);
    return false;
  }

  spdlog::info(
      "[MapBaker] Successfully baked {} chunks from '{}' -> '{}' (Spawn: {:.1f}, {:.1f}, {:.1f})",
      world.chunkCount(),
      in_str,
      out_str,
      world.spawnPoint().x,
      world.spawnPoint().y,
      world.spawnPoint().z
  );

  // 伴生 .ini 配置生成：
  // 仅在伴生 ini 不存在时自动生成，存放于与 map 相同的目录下
  const auto stem = in_path.stem().string();
  const std::filesystem::path ini_path = out_path.parent_path() / (stem + ".ini");
  const std::filesystem::path lobby_default_ini = out_path.parent_path() / "lobby.ini";

  if (!std::filesystem::exists(ini_path) && !std::filesystem::exists(lobby_default_ini)) {
    std::ofstream ofs(ini_path);
    if (ofs.is_open()) {
      const auto sp = world.spawnPoint();
      ofs << "; " << stem << " Lobby Map Companion Configuration\n";
      ofs << "[World]\n";
      ofs << "baked_file = " << out_path.generic_string() << "\n";
      ofs << "spawn_x = " << sp.x << "\n";
      ofs << "spawn_y = " << sp.y << "\n";
      ofs << "spawn_z = " << sp.z << "\n";
      ofs << "spawn_yaw = " << world.spawnRotation().yaw << "\n";
      ofs << "spawn_pitch = " << world.spawnRotation().pitch << "\n";
      ofs << "fall_reset_y = 20.00\n";
      const std::string ini_str = ini_path.string();
      spdlog::info("[MapBaker] Generated lobby companion config: {}", ini_str);
    }
  }

  return true;
}

void
scanAndBakeDirectory(
    const std::filesystem::path& dir_path,
    size_t& success_count
) {
  if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
    return;
  }

  for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
    if (!entry.is_regular_file()) continue;
    const auto ext = entry.path().extension().string();
    if (ext == ".zip" || ext == ".rar" || ext == ".schematic") {
      // 烘焙输出至与源文件相同的目录下：<parent>/<stem>.mcpe_map
      const auto out_file = entry.path().parent_path() / (entry.path().stem().string() + ".mcpe_map");
      if (bakeAndSave(entry.path(), out_file)) {
        ++success_count;
      }
    }
  }
}

}  // namespace

int
main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::info);
  spdlog::info("================================================");
  spdlog::info(" MCPE Offline Map Baker (.zip/.rar/.schematic -> .mcpe_map)");
  spdlog::info(" Target: Bake lobby source maps into same directory");
  spdlog::info("================================================");

  // 1. 如果传参，执行单地图指定烘焙
  if (argc >= 2) {
    const std::filesystem::path in_path = argv[1];
    std::filesystem::path out_path;
    if (argc >= 3) {
      out_path = argv[2];
    } else {
      // 默认输出至与源文件相同目录下同名 .mcpe_map
      out_path = in_path.parent_path() / (in_path.stem().string() + ".mcpe_map");
    }
    const bool ok = bakeAndSave(in_path, out_path);
    return ok ? 0 : 1;
  }

  // 2. 无参模式：自动扫描 assets/maps/lobby 下的所有源文件并烘焙至同目录
  size_t success_count = 0;
  const std::filesystem::path lobby_dir = findLobbyDir();

  spdlog::info("[MapBaker] Scanning '{}' for source maps...", lobby_dir.string());
  scanAndBakeDirectory(lobby_dir, success_count);

  spdlog::info("================================================");
  spdlog::info(
      "[MapBaker] Batch baking completed: {} map(s) baked successfully.",
      success_count
  );
  spdlog::info("================================================");

  return 0;
}
