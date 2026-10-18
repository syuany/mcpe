/**
 * @file test_spatial_grid.cc
 * @brief SpatialGrid 九宫格空间桶与邻居差异集单元测试
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "world/SpatialGrid.h"

using namespace mcpe;
using namespace mcpe::world;

TEST(SpatialGridTest, AddAndQueryNearbyPlayers) {
  SpatialGrid grid;

  // 玩家 1 在原点 (0, 0)
  grid.addPlayer(1, ChunkPos{0, 0});
  // 玩家 2 在邻接对角区块 (1, 1)
  grid.addPlayer(2, ChunkPos{1, 1});
  // 玩家 3 在远端区块 (5, 5)
  grid.addPlayer(3, ChunkPos{5, 5});

  EXPECT_EQ(grid.playerCount(), 3);
  EXPECT_EQ(grid.getPlayerChunk(1), (ChunkPos{0, 0}));
  EXPECT_EQ(grid.getPlayerChunk(2), (ChunkPos{1, 1}));
  EXPECT_EQ(grid.getPlayerChunk(3), (ChunkPos{5, 5}));

  // 以 (0, 0) 为中心九宫格 (radius=1)：涵盖 [-1, 1] x [-1, 1]
  auto nearby = grid.getNearbyPlayers(ChunkPos{0, 0}, 1);
  EXPECT_EQ(nearby.size(), 2);
  EXPECT_TRUE(std::find(nearby.begin(), nearby.end(), 1) != nearby.end());
  EXPECT_TRUE(std::find(nearby.begin(), nearby.end(), 2) != nearby.end());
  EXPECT_FALSE(std::find(nearby.begin(), nearby.end(), 3) != nearby.end());  // 远端不可见！
}

TEST(SpatialGridTest, CrossChunkBoundaryDifferenceSets) {
  SpatialGrid grid;
  grid.addPlayer(1, ChunkPos{0, 0});

  // 1. 同区块微移：不触发跨界
  auto trans_same = grid.updatePlayerChunk(1, ChunkPos{0, 0}, 1);
  EXPECT_FALSE(trans_same.crossed);
  EXPECT_TRUE(trans_same.enter_chunks.empty());
  EXPECT_TRUE(trans_same.leave_chunks.empty());

  // 2. 向东跨越 1 个区块：从 (0, 0) -> (1, 0)
  auto trans_east = grid.updatePlayerChunk(1, ChunkPos{1, 0}, 1);
  EXPECT_TRUE(trans_east.crossed);
  EXPECT_EQ(trans_east.old_chunk, (ChunkPos{0, 0}));
  EXPECT_EQ(trans_east.new_chunk, (ChunkPos{1, 0}));

  // 新进入东侧 3 个区块: (2, -1), (2, 0), (2, 1)
  EXPECT_EQ(trans_east.enter_chunks.size(), 3);
  EXPECT_TRUE(std::find(trans_east.enter_chunks.begin(), trans_east.enter_chunks.end(), ChunkPos{2, -1}) != trans_east.enter_chunks.end());
  EXPECT_TRUE(std::find(trans_east.enter_chunks.begin(), trans_east.enter_chunks.end(), ChunkPos{2, 0}) != trans_east.enter_chunks.end());
  EXPECT_TRUE(std::find(trans_east.enter_chunks.begin(), trans_east.enter_chunks.end(), ChunkPos{2, 1}) != trans_east.enter_chunks.end());

  // 移出西侧 3 个区块: (-1, -1), (-1, 0), (-1, 1)
  EXPECT_EQ(trans_east.leave_chunks.size(), 3);
  EXPECT_TRUE(std::find(trans_east.leave_chunks.begin(), trans_east.leave_chunks.end(), ChunkPos{-1, -1}) != trans_east.leave_chunks.end());
  EXPECT_TRUE(std::find(trans_east.leave_chunks.begin(), trans_east.leave_chunks.end(), ChunkPos{-1, 0}) != trans_east.leave_chunks.end());
  EXPECT_TRUE(std::find(trans_east.leave_chunks.begin(), trans_east.leave_chunks.end(), ChunkPos{-1, 1}) != trans_east.leave_chunks.end());
}

TEST(SpatialGridTest, RemovePlayerCleansUp) {
  SpatialGrid grid;
  grid.addPlayer(1, ChunkPos{0, 0});
  EXPECT_TRUE(grid.hasPlayer(1));

  grid.removePlayer(1);
  EXPECT_FALSE(grid.hasPlayer(1));
  EXPECT_EQ(grid.playerCount(), 0);

  auto nearby = grid.getNearbyPlayers(ChunkPos{0, 0}, 1);
  EXPECT_TRUE(nearby.empty());
}
