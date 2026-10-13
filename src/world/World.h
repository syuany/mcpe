/**
 * @file World.h
 * @brief 世界容器与区块内存常驻管理器（Domain 领域层）
 *
 * 核心规范：
 * 1. 负责集中管理内存中的所有 Chunk 垂直柱；
 * 2. 支持 WorldEdit .schematic 建筑蓝图的动态加载与填充；
 * 3. 维护世界的 Spawn Point 出生点坐标；
 * 4. 纯领域对象，与网络 Socket / TcpSession 完全解耦。
 */

#ifndef MCPE_WORLD_WORLD_H
#define MCPE_WORLD_WORLD_H

#include <filesystem>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "common/SpatialTypes.h"
#include "world/Chunk.h"
#include "world/SchematicLoader.h"

namespace mcpe::world {

class World {
 public:
  World() = default;

  /**
   * @brief 设置/获取世界默认出生点
   */
  void setSpawnPoint(const Vector3d& spawn) noexcept { spawn_point_ = spawn; }
  [[nodiscard]] const Vector3d& spawnPoint() const noexcept { return spawn_point_; }

  /**
   * @brief 查询指定区块指针（不存在则返回 nullptr）
   */
  [[nodiscard]] const Chunk* getChunk(ChunkPos pos) const;
  [[nodiscard]] Chunk* getChunk(ChunkPos pos);

  /**
   * @brief 获取或动态创建指定坐标的 Chunk
   */
  Chunk& getOrCreateChunk(ChunkPos pos);

  /**
   * @brief 检查世界是否包含指定区块
   */
  [[nodiscard]] bool hasChunk(ChunkPos pos) const noexcept {
    return chunks_.contains(pos);
  }

  /**
   * @brief 从 Schematic 蓝图加载并填充方块至世界内存
   * @param schematic 蓝图对象
   * @param origin 放置在世界中的左下角最小坐标 (X, Y, Z)
   */
  void loadSchematic(const Schematic& schematic, const BlockPos& origin);

  /**
   * @brief 从指定文件加载大厅蓝图
   */
  bool loadLobbySchematic(const std::filesystem::path& path, const BlockPos& origin);

  /**
   * @brief 从原版世界存档 ZIP 包（包含 level.dat 与 region 区域文件）加载世界并设置出生点
   * @param path ZIP 文件路径
   * @return 成功加载的 Chunk 总数
   */
  size_t loadWorldFromZip(const std::filesystem::path& path);

  /**
   * @brief 从原版世界存档目录加载世界并设置出生点
   * @param path 存档根目录路径
   * @return 成功加载的 Chunk 总数
   */
  size_t loadWorldFromDirectory(const std::filesystem::path& path);

  /**
   * @brief 查询指定世界水平坐标 (X, Z) 的最高非空气方块高度 Y，若该垂直列无方块则返回 -1
   */
  [[nodiscard]] int32_t getHighestBlockY(int32_t x, int32_t z) const;

  /**
   * @brief 将当前出生点自适应修正到安全站立地面上（防止地图作者创造模式飞行保存或虚空出生点）
   */
  void adjustSpawnPointToSafeGround();

  /**
   * @brief 获取当前世界中所有已加载区块的坐标列表
   */
  [[nodiscard]] size_t chunkCount() const noexcept { return chunks_.size(); }
  [[nodiscard]] std::vector<ChunkPos> getAllChunkPositions() const;

 private:
  Vector3d spawn_point_{0.5, 64.0, 0.5};
  std::unordered_map<ChunkPos, std::unique_ptr<Chunk>> chunks_;
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_WORLD_H
