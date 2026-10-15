/**
 * @file test_schematic.cc
 * @brief 地图烘焙产物与 SchematicLoader 输入校验测试
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

#include "SchematicLoader.h"
#include "common/SpatialTypes.h"
#include "world/Chunk.h"
#include "world/World.h"

using namespace mcpe;
using namespace mcpe::world;

namespace {
std::filesystem::path findLobbyMap() {
  if (std::filesystem::exists("assets/maps/lobby.mcpe_map")) {
    return "assets/maps/lobby.mcpe_map";
  }
  if (std::filesystem::exists("../assets/maps/lobby.mcpe_map")) {
    return "../assets/maps/lobby.mcpe_map";
  }
  if (std::filesystem::exists("../../assets/maps/lobby.mcpe_map")) {
    return "../../assets/maps/lobby.mcpe_map";
  }
  return "assets/maps/lobby.mcpe_map";
}
}  // namespace

TEST(BakedMapTest, LoadLobbyMapFromFile) {
  const std::filesystem::path map_path = findLobbyMap();
  ASSERT_TRUE(std::filesystem::exists(map_path)) << "assets/maps/lobby.mcpe_map must exist";

  World world;
  EXPECT_EQ(world.loadBakedMap(map_path), 54u);
  EXPECT_EQ(world.chunkCount(), 54u);
  EXPECT_NEAR(world.spawnPoint().x, -103.9125, 0.001);
  EXPECT_DOUBLE_EQ(world.spawnPoint().y, 55.0);
  EXPECT_NEAR(world.spawnPoint().z, -35.0046, 0.001);
}

TEST(BakedMapTest, LoadLobbyMapChunkData) {
  const std::filesystem::path map_path = findLobbyMap();
  World world;
  ASSERT_EQ(world.loadBakedMap(map_path), 54u);

  const Chunk* spawn_chunk = world.getChunk(ChunkPos{-7, -3});
  EXPECT_NE(spawn_chunk, nullptr);
}

TEST(SchematicLoaderTest, CorruptedOrEmptyInputHandling) {
  std::vector<uint8_t> empty_data;
  EXPECT_FALSE(SchematicLoader::loadFromCompressedMemory(empty_data).has_value());

  std::vector<uint8_t> garbage = {0x1F, 0x8B, 0x00, 0xFF, 0xFE};
  EXPECT_FALSE(SchematicLoader::loadFromCompressedMemory(garbage).has_value());
}
