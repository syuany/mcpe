/**
 * @file AnvilLoader.h
 * @brief Minecraft 原版 Anvil (.mca) 与世界存档 (.zip / 文件夹) 加载器（Domain 领域层）
 *
 * 核心规范：
 * 1. 深度适配 Minecraft 1.12.2 (Protocol 340) 原版 Anvil 格式；
 * 2. 自动从 level.dat 解析并提取官方 SpawnPoint 出生点坐标；
 * 3. 流式解析 region/r.X.Z.mca 容器中的所有有效 ChunkSections (Blocks / Data)；
 * 4. 原生支持 .zip 格式世界存档（如大厅 world.zip）及解压文件夹。
 */

#ifndef MCPE_WORLD_ANVIL_LOADER_H
#define MCPE_WORLD_ANVIL_LOADER_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "common/SpatialTypes.h"
#include "world/World.h"

namespace mcpe::world {

/**
 * @brief 世界存档基础元数据
 */
struct WorldSaveMetadata {
  std::string level_name{"world"};
  Vector3d spawn_point{0.5, 64.0, 0.5};
  std::optional<Vector3d> player_pos{std::nullopt};
};

/**
 * @brief Anvil (.mca) 与 World Save 格式解析器
 */
class AnvilLoader {
 public:
  /**
   * @brief 从 level.dat 的二进制字节流（Gzip 压缩）中提取世界元数据与出生点
   */
  [[nodiscard]] static std::optional<WorldSaveMetadata> parseLevelDat(
      std::span<const uint8_t> level_dat_bytes
  );

  /**
   * @brief 解析单个 .mca (Region) 文件的内存数据并灌入 World
   * @param mca_bytes .mca 文件原始内容
   * @param world 目标世界容器
   * @return 成功解析并导入的有效 Chunk 数量
   */
  static size_t parseRegionFile(
      std::span<const uint8_t> mca_bytes,
      World& world
  );

  /**
   * @brief 从指定 ZIP 压缩包（如大厅 world.zip）加载完整世界
   * @param zip_path ZIP 文件路径
   * @param world 目标世界容器
   * @return 成功加载的 Chunk 总数
   */
  static size_t loadFromZip(
      const std::filesystem::path& zip_path,
      World& world
  );

  /**
   * @brief 从指定磁盘世界目录（包含 level.dat 和 region/ 文件夹）加载完整世界
   * @param dir_path 文件夹路径
   * @param world 目标世界容器
   * @return 成功加载的 Chunk 总数
   */
  static size_t loadFromDirectory(
      const std::filesystem::path& dir_path,
      World& world
  );

  /**
   * @brief 从指定 RAR 压缩包解压并加载完整世界
   * @param rar_path RAR 文件路径
   * @param world 目标世界容器
   * @return 成功加载的 Chunk 总数
   */
  static size_t loadFromRar(
      const std::filesystem::path& rar_path,
      World& world
  );
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_ANVIL_LOADER_H
