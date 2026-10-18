/**
 * @file test_world.cc
 * @brief World 容器、预烘焙地图镜像 (.mcpe_map) 与出生点配置持久化单元测试
 */

#include <gtest/gtest.h>

#include <filesystem>

#include "world/World.h"

using namespace mcpe;
using namespace mcpe::world;

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

TEST(WorldTest, SaveAndLoadBakedMapRoundTrip) {
  const std::filesystem::path tmp_map = "/tmp/test_baked_lobby.mcpe_map";

  World src_world;
  src_world.setSpawnPoint(Vector3d{48.5, 34.0, 48.5});
  src_world.setSpawnRotation(Rotation{90.0f, 15.0f});

  Chunk& c1 = src_world.getOrCreateChunk(ChunkPos{3, 3});
  c1.setBlock(BlockPos{48, 37, 48}, toGlobalState(169, 0));  // Sea Lantern
  c1.setBlock(BlockPos{48, 38, 48}, toGlobalState(9, 0));    // Water

  Chunk& c2 = src_world.getOrCreateChunk(ChunkPos{0, 0});
  c2.setBlock(BlockPos{0, 0, 0}, BlockStates::kBedrock);

  ASSERT_TRUE(src_world.saveBakedMap(tmp_map));

  World dst_world;
  size_t loaded = dst_world.loadBakedMap(tmp_map);
  EXPECT_EQ(loaded, 2u);
  EXPECT_EQ(dst_world.chunkCount(), 2u);
  EXPECT_DOUBLE_EQ(dst_world.spawnPoint().x, 48.5);
  EXPECT_DOUBLE_EQ(dst_world.spawnPoint().y, 34.0);
  EXPECT_DOUBLE_EQ(dst_world.spawnPoint().z, 48.5);
  EXPECT_FLOAT_EQ(dst_world.spawnRotation().yaw, 90.0f);
  EXPECT_FLOAT_EQ(dst_world.spawnRotation().pitch, 15.0f);

  const Chunk* loaded_c1 = dst_world.getChunk(ChunkPos{3, 3});
  ASSERT_NE(loaded_c1, nullptr);
  EXPECT_EQ(loaded_c1->getBlock(BlockPos{48, 37, 48}) >> 4, 169);
  EXPECT_EQ(loaded_c1->getBlock(BlockPos{48, 38, 48}) >> 4, 9);

  std::filesystem::remove(tmp_map);
}

TEST(WorldTest, SpawnConfigPersistenceRoundTrip) {
  const std::filesystem::path tmp_cfg = "/tmp/test_spawn_cfg.json";

  World w1;
  w1.setSpawnPoint(Vector3d{-56.25, 52.0, 8.75});
  w1.setSpawnRotation(Rotation{180.0f, -10.5f});
  ASSERT_TRUE(w1.saveSpawnConfig(tmp_cfg));

  World w2;
  ASSERT_TRUE(w2.loadSpawnConfig(tmp_cfg));
  EXPECT_NEAR(w2.spawnPoint().x, -56.25, 0.01);
  EXPECT_NEAR(w2.spawnPoint().y, 52.0, 0.01);
  EXPECT_NEAR(w2.spawnPoint().z, 8.75, 0.01);
  EXPECT_NEAR(w2.spawnRotation().yaw, 180.0f, 0.01f);
  EXPECT_NEAR(w2.spawnRotation().pitch, -10.5f, 0.01f);

  std::filesystem::remove(tmp_cfg);
}
