/**
 * @file SpatialGrid.h
 * @brief 基于 ChunkPos 空间桶与九宫格邻居差异集网格管理器（Domain 领域层）
 *
 * 核心规范：
 * 1. 维护玩家 SessionID 与所在 ChunkPos 的双向映射；
 * 2. 提供获取指定区块 R 邻域（九宫格）在线玩家会话集合接口；
 * 3. 玩家跨区块移动时，高精度计算进入 (Enter) 与离开 (Leave) 区块差集。
 */

#ifndef MCPE_WORLD_SPATIAL_GRID_H
#define MCPE_WORLD_SPATIAL_GRID_H

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "common/SpatialTypes.h"

namespace mcpe::world {

/**
 * @brief 玩家跨区块边界时的视野差集结果
 */
struct ChunkTransition {
  bool crossed{false};
  ChunkPos old_chunk{0, 0};
  ChunkPos new_chunk{0, 0};
  std::vector<ChunkPos> enter_chunks;  // 新进入视野的区块集合
  std::vector<ChunkPos> leave_chunks;  // 移出视野的区块集合
};

class SpatialGrid {
 public:
  static constexpr int32_t kDefaultRadius = 1;  // 默认 3x3 九宫格 (半径 1)

  SpatialGrid() = default;

  /**
   * @brief 注册玩家进入网格
   */
  void addPlayer(uint64_t session_id, ChunkPos chunk_pos);

  /**
   * @brief 玩家离开网格
   */
  void removePlayer(uint64_t session_id);

  /**
   * @brief 更新玩家所在区块坐标；若发生跨区块移动，计算进入与移出差集
   */
  ChunkTransition updatePlayerChunk(
      uint64_t session_id,
      ChunkPos new_chunk,
      int32_t radius = kDefaultRadius
  );

  /**
   * @brief 获取指定中心区块周围半径 radius 内的所有在线玩家会话 ID
   */
  [[nodiscard]] std::vector<uint64_t> getNearbyPlayers(
      ChunkPos center,
      int32_t radius = kDefaultRadius
  ) const;

  /**
   * @brief 获取指定玩家当前所在的区块坐标
   */
  [[nodiscard]] ChunkPos getPlayerChunk(uint64_t session_id) const;

  /**
   * @brief 检查玩家是否已在网格中注册
   */
  [[nodiscard]] bool hasPlayer(uint64_t session_id) const noexcept {
    return player_chunks_.contains(session_id);
  }

  /**
   * @brief 获取当前网格中的总玩家数
   */
  [[nodiscard]] size_t playerCount() const noexcept {
    return player_chunks_.size();
  }

  /**
   * @brief 工具函数：计算从 old_center 移动到 new_center 时的九宫格差集
   */
  static ChunkTransition computeTransition(
      ChunkPos old_center,
      ChunkPos new_center,
      int32_t radius = kDefaultRadius
  );

 private:
  std::unordered_map<ChunkPos, std::vector<uint64_t>> chunk_players_;
  std::unordered_map<uint64_t, ChunkPos> player_chunks_;
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_SPATIAL_GRID_H
