/**
 * @file SpatialGrid.cc
 * @brief 基于 ChunkPos 空间桶与九宫格邻居差异集网格管理器实现
 */

#include "world/SpatialGrid.h"

#include <algorithm>
#include <cmath>

namespace mcpe::world {

void
SpatialGrid::addPlayer(uint64_t session_id, ChunkPos chunk_pos) {
  // 防御性去重
  removePlayer(session_id);

  player_chunks_[session_id] = chunk_pos;
  chunk_players_[chunk_pos].push_back(session_id);
}

void
SpatialGrid::removePlayer(uint64_t session_id) {
  auto it = player_chunks_.find(session_id);
  if (it == player_chunks_.end()) {
    return;
  }

  const ChunkPos old_pos = it->second;
  player_chunks_.erase(it);

  auto chunk_it = chunk_players_.find(old_pos);
  if (chunk_it != chunk_players_.end()) {
    auto& vec = chunk_it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), session_id), vec.end());
    if (vec.empty()) {
      chunk_players_.erase(chunk_it);
    }
  }
}

ChunkTransition
SpatialGrid::computeTransition(
    ChunkPos old_center,
    ChunkPos new_center,
    int32_t radius
) {
  ChunkTransition result;
  result.old_chunk = old_center;
  result.new_chunk = new_center;

  if (old_center == new_center) {
    result.crossed = false;
    return result;
  }

  result.crossed = true;

  // 1. 扫描新窗口中的每个区块，若不在旧窗口内则为 enter_chunks
  for (int32_t z = new_center.z - radius; z <= new_center.z + radius; ++z) {
    for (int32_t x = new_center.x - radius; x <= new_center.x + radius; ++x) {
      if (std::abs(x - old_center.x) > radius || std::abs(z - old_center.z) > radius) {
        result.enter_chunks.push_back(ChunkPos{x, z});
      }
    }
  }

  // 2. 扫描旧窗口中的每个区块，若不在新窗口内则为 leave_chunks
  for (int32_t z = old_center.z - radius; z <= old_center.z + radius; ++z) {
    for (int32_t x = old_center.x - radius; x <= old_center.x + radius; ++x) {
      if (std::abs(x - new_center.x) > radius || std::abs(z - new_center.z) > radius) {
        result.leave_chunks.push_back(ChunkPos{x, z});
      }
    }
  }

  return result;
}

ChunkTransition
SpatialGrid::updatePlayerChunk(
    uint64_t session_id,
    ChunkPos new_chunk,
    int32_t radius
) {
  auto it = player_chunks_.find(session_id);
  if (it == player_chunks_.end()) {
    addPlayer(session_id, new_chunk);
    ChunkTransition trans;
    trans.crossed = true;
    trans.new_chunk = new_chunk;
    for (int32_t z = new_chunk.z - radius; z <= new_chunk.z + radius; ++z) {
      for (int32_t x = new_chunk.x - radius; x <= new_chunk.x + radius; ++x) {
        trans.enter_chunks.push_back(ChunkPos{x, z});
      }
    }
    return trans;
  }

  const ChunkPos old_chunk = it->second;
  if (old_chunk == new_chunk) {
    ChunkTransition trans;
    trans.crossed = false;
    trans.old_chunk = old_chunk;
    trans.new_chunk = new_chunk;
    return trans;
  }

  // 迁移会话到新区块
  auto old_it = chunk_players_.find(old_chunk);
  if (old_it != chunk_players_.end()) {
    auto& vec = old_it->second;
    vec.erase(std::remove(vec.begin(), vec.end(), session_id), vec.end());
    if (vec.empty()) {
      chunk_players_.erase(old_it);
    }
  }

  player_chunks_[session_id] = new_chunk;
  chunk_players_[new_chunk].push_back(session_id);

  return computeTransition(old_chunk, new_chunk, radius);
}

std::vector<uint64_t>
SpatialGrid::getNearbyPlayers(ChunkPos center, int32_t radius) const {
  std::vector<uint64_t> nearby;

  for (int32_t z = center.z - radius; z <= center.z + radius; ++z) {
    for (int32_t x = center.x - radius; x <= center.x + radius; ++x) {
      auto it = chunk_players_.find(ChunkPos{x, z});
      if (it != chunk_players_.end()) {
        nearby.insert(nearby.end(), it->second.begin(), it->second.end());
      }
    }
  }

  return nearby;
}

ChunkPos
SpatialGrid::getPlayerChunk(uint64_t session_id) const {
  auto it = player_chunks_.find(session_id);
  if (it != player_chunks_.end()) {
    return it->second;
  }
  return ChunkPos{0, 0};
}

}  // namespace mcpe::world
