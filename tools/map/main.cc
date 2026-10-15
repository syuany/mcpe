/**
 * @file main.cc
 * @brief MCPE 离线地图烘焙器命令行工具 (tools/map_baker)
 *
 * 功能定位：
 * 将沉重的 Zlib 解压、PKZIP 归档解析、NBT 树遍历与 Anvil (.mca) / WorldEdit (.schematic)
 * 解析工作彻底前置到离线阶段，输出零解压开销的紧凑二进制镜像文件 (.mcpe_map)，
 * 从而让线上 MCPE 服务端实现 <1ms 极速冷启动并摆脱对 Zlib 的运行时依赖。
 *
 * 用法：
 *   ./bin/map_baker [input_path] [output_path]
 *   - 若不传参数，默认自动扫描 assets/maps/lobby 目录下的 .zip 或 assets/maps/lobby.schematic，
 *     烘焙输出至 assets/maps/lobby.mcpe_map。
 */

#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "AnvilLoader.h"
#include "SchematicLoader.h"
#include "world/World.h"

using namespace mcpe;
using namespace mcpe::world;

namespace {

bool
bakeFromInput(const std::filesystem::path& input_path, World& world) {
  if (!std::filesystem::exists(input_path)) {
    spdlog::error("[MapBaker] Input path does not exist: {}", input_path.string());
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

  if (ext == ".schematic") {
    if (!SchematicLoader::loadIntoWorld(input_path, BlockPos{0, 0, 0}, world)) {
      return false;
    }
    // 浮空岛中心默认出生点并自适应安全地面高度
    world.setSpawnPoint(Vector3d{48.5, 34.0, 48.5});
    world.adjustSpawnPointToSafeGround();
    return world.chunkCount() > 0;
  }

  spdlog::error("[MapBaker] Unsupported input format: {}", input_path.string());
  return false;
}

}  // namespace

int
main(int argc, char* argv[]) {
  spdlog::set_level(spdlog::level::info);
  spdlog::info("================================================");
  spdlog::info(" MCPE Offline Map Baker (.zip/.schematic -> .mcpe_map)");
  spdlog::info("================================================");

  std::filesystem::path input_path;
  std::filesystem::path output_path = "assets/maps/lobby.mcpe_map";

  if (argc >= 2) {
    input_path = argv[1];
  }
  if (argc >= 3) {
    output_path = argv[2];
  }

  World world;
  bool loaded = false;

  if (!input_path.empty()) {
    loaded = bakeFromInput(input_path, world);
  } else {
    // 默认自动扫描 assets/maps/lobby 下的 .zip，次选 assets/maps/lobby.schematic
    const std::filesystem::path lobby_dir = "assets/maps/lobby";
    if (std::filesystem::exists(lobby_dir) && std::filesystem::is_directory(lobby_dir)) {
      for (const auto& entry : std::filesystem::directory_iterator(lobby_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".zip") {
          spdlog::info("[MapBaker] Auto-discovered ZIP archive: {}", entry.path().string());
          if (bakeFromInput(entry.path(), world)) {
            input_path = entry.path();
            loaded = true;
            break;
          }
        }
      }
    }
    if (!loaded && std::filesystem::exists("assets/maps/lobby.schematic")) {
      input_path = "assets/maps/lobby.schematic";
      spdlog::info("[MapBaker] Auto-discovered schematic: {}", input_path.string());
      loaded = bakeFromInput(input_path, world);
    }
  }

  if (!loaded) {
    spdlog::error("[MapBaker] Failed to load any source map for baking.");
    return 1;
  }

  if (!world.saveBakedMap(output_path)) {
    spdlog::error("[MapBaker] Failed to write baked map to '{}'", output_path.string());
    return 1;
  }

  spdlog::info(
      "[MapBaker] Successfully baked {} chunks from '{}' -> '{}' (Spawn: {:.1f}, {:.1f}, {:.1f})",
      world.chunkCount(),
      input_path.string(),
      output_path.string(),
      world.spawnPoint().x,
      world.spawnPoint().y,
      world.spawnPoint().z
  );
  return 0;
}
