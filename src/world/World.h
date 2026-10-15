/**
 * @file World.h
 * @brief 世界容器与区块内存常驻管理器（Domain 领域层）
 *
 * 核心规范：
 * 1. 负责集中管理内存中的所有 Chunk 垂直柱；
 * 2. 支持预烘焙地图二进制镜像（.mcpe_map）的极速序列化与反序列化，彻底剥离 Zlib/NBT 运行时依赖；
 * 3. 线程安全地维护世界的 Spawn Point 出生点坐标与持久化配置（spawn.json）；
 * 4. 纯领域对象，与网络 Socket / TcpSession 完全解耦。
 */

#ifndef MCPE_WORLD_WORLD_H
#define MCPE_WORLD_WORLD_H

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "common/SpatialTypes.h"
#include "world/Chunk.h"

namespace mcpe::world {

class World {
 public:
  World() = default;

  /**
   * @brief 线程安全地设置/获取世界默认出生点坐标与朝向
   */
  void
  setSpawnPoint(const Vector3d& spawn) noexcept {
    std::lock_guard<std::mutex> lock(spawn_mutex_);
    spawn_point_ = spawn;
  }

  void
  setSpawnRotation(const Rotation& rot) noexcept {
    std::lock_guard<std::mutex> lock(spawn_mutex_);
    spawn_rotation_ = rot;
  }

  [[nodiscard]] Vector3d
  spawnPoint() const noexcept {
    std::lock_guard<std::mutex> lock(spawn_mutex_);
    return spawn_point_;
  }

  [[nodiscard]] Rotation
  spawnRotation() const noexcept {
    std::lock_guard<std::mutex> lock(spawn_mutex_);
    return spawn_rotation_;
  }

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
  [[nodiscard]] bool
  hasChunk(ChunkPos pos) const noexcept {
    return chunks_.contains(pos);
  }

  /**
   * @brief 将当前世界中的所有区块与出生点导出为紧凑的预烘焙二进制镜像 (.mcpe_map)
   */
  [[nodiscard]] bool saveBakedMap(const std::filesystem::path& path) const;

  /**
   * @brief 从预烘焙二进制镜像 (.mcpe_map) 极速加载世界（零解压开销，冷启动 < 1ms）
   * @return 成功加载的 Chunk 总数
   */
  size_t loadBakedMap(const std::filesystem::path& path);

  /**
   * @brief 从 JSON 配置文件加载自定义出生点坐标与视角（例如 assets/maps/lobby/spawn.json）
   */
  bool loadSpawnConfig(const std::filesystem::path& path);

  /**
   * @brief 将当前世界出生点坐标与视角持久化写入 JSON 配置文件
   */
  bool saveSpawnConfig(const std::filesystem::path& path) const;

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
  [[nodiscard]] size_t
  chunkCount() const noexcept {
    return chunks_.size();
  }
  [[nodiscard]] std::vector<ChunkPos> getAllChunkPositions() const;

 private:
  mutable std::mutex spawn_mutex_;
  Vector3d spawn_point_{0.5, 64.0, 0.5};
  Rotation spawn_rotation_{0.0f, 0.0f};
  std::unordered_map<ChunkPos, std::unique_ptr<Chunk>> chunks_;
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_WORLD_H
