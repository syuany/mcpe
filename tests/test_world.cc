/**
 * @file test_world.cc
 * @brief World 容器与大厅蓝图常驻内存单元测试
 */

#include <gtest/gtest.h>

#include <filesystem>

#include "world/World.h"

using namespace mcpe;
using namespace mcpe::world;

namespace {
std::filesystem::path findLobbySchematic() {
  if (std::filesystem::exists("assets/maps/lobby.schematic")) {
    return "assets/maps/lobby.schematic";
  }
  if (std::filesystem::exists("../assets/maps/lobby.schematic")) {
    return "../assets/maps/lobby.schematic";
  }
  if (std::filesystem::exists("../../assets/maps/lobby.schematic")) {
    return "../../assets/maps/lobby.schematic";
  }
  return "assets/maps/lobby.schematic";
}
}  // namespace

TEST(WorldTest, DefaultStateAndSpawnPoint) {
  World world;
  EXPECT_EQ(world.chunkCount(), 0);
  EXPECT_FALSE(world.hasChunk(ChunkPos{0, 0}));

  world.setSpawnPoint(Vector3d{0.5, 64.0, 0.5});
  EXPECT_DOUBLE_EQ(world.spawnPoint().x, 0.5);
  EXPECT_DOUBLE_EQ(world.spawnPoint().y, 64.0);
  EXPECT_DOUBLE_EQ(world.spawnPoint().z, 0.5);
}

TEST(WorldTest, GetOrCreateChunkAndBlockOperations) {
  World world;
  Chunk& chunk = world.getOrCreateChunk(ChunkPos{1, -1});
  EXPECT_EQ(world.chunkCount(), 1);
  EXPECT_TRUE(world.hasChunk(ChunkPos{1, -1}));

  chunk.setBlock(BlockPos{16, 4, -16}, BlockStates::kGrass);
  EXPECT_EQ(chunk.getBlock(BlockPos{16, 4, -16}), BlockStates::kGrass);

  const Chunk* found = world.getChunk(ChunkPos{1, -1});
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->getBlock(BlockPos{16, 4, -16}), BlockStates::kGrass);

  EXPECT_EQ(world.getChunk(ChunkPos{2, 2}), nullptr);
}

TEST(WorldTest, LoadLobbySchematicCreatesChunks) {
  World world;
  const auto path = findLobbySchematic();
  ASSERT_TRUE(std::filesystem::exists(path));

  // 将大厅放置于世界原点 (0, 0, 0)
  bool success = world.loadLobbySchematic(path, BlockPos{0, 0, 0});
  ASSERT_TRUE(success);

  // 96x96 的蓝图放置在 (0, 0)，跨越 X: [0, 95] (6 个区块)，Z: [0, 95] (6 个区块)
  // 共计 6 x 6 = 36 个区块
  EXPECT_EQ(world.chunkCount(), 36);

  // 检查中心区块 ChunkPos(3, 3) 存在且包含喷泉水方块
  const Chunk* center = world.getChunk(ChunkPos{3, 3});
  ASSERT_NE(center, nullptr);
  // (48, 38, 48) 是水 (Block ID 9)
  EXPECT_EQ(center->getBlock(BlockPos{48, 38, 48}) >> 4, 9);
}
