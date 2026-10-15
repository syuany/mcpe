/**
 * @file test_schematic.cc
 * @brief SchematicLoader 建筑蓝图解析与区块粘贴单元测试
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

TEST(SchematicLoaderTest, LoadLobbySchematicFromFile) {
  const std::filesystem::path schem_path = findLobbySchematic();
  ASSERT_TRUE(std::filesystem::exists(schem_path)) << "assets/maps/lobby.schematic must exist";

  auto schem_opt = SchematicLoader::loadFromFile(schem_path);
  ASSERT_TRUE(schem_opt.has_value());

  const auto& schem = *schem_opt;
  EXPECT_EQ(schem.width, 96);
  EXPECT_EQ(schem.height, 48);
  EXPECT_EQ(schem.length, 96);
  EXPECT_TRUE(schem.isValid());
  EXPECT_EQ(schem.totalBlocks(), 96 * 48 * 96);

  // 1. 验证中心喷泉平台方块状态 (cx=48, surface_y=32, cz=48)
  const uint16_t water_state = schem.getBlockState(48, 38, 48);
  EXPECT_EQ(water_state >> 4, 9);  // Water

  const uint16_t sea_lantern_state = schem.getBlockState(48, 37, 48);
  EXPECT_EQ(sea_lantern_state >> 4, 169);  // Sea Lantern

  // 2. 验证四个方位的传送门黑曜石门框 (Block ID 49)
  const uint16_t obsidian_state = schem.getBlockState(47, 33, 30);
  EXPECT_EQ(obsidian_state >> 4, 49);  // Obsidian

  // 3. 验证外围虚空区域 (0, 0, 0) 是空气 (Block ID 0)
  EXPECT_EQ(schem.getBlockState(0, 0, 0), 0);
  EXPECT_EQ(schem.getBlockState(-1, 0, 0), 0);
  EXPECT_EQ(schem.getBlockState(100, 100, 100), 0);
}

TEST(SchematicLoaderTest, PasteToChunkAndWorldIntersection) {
  const std::filesystem::path schem_path = findLobbySchematic();
  World world;
  ASSERT_TRUE(SchematicLoader::loadIntoWorld(schem_path, BlockPos{0, 0, 0}, world));
  EXPECT_EQ(world.chunkCount(), 36u);

  const Chunk* center = world.getChunk(ChunkPos{3, 3});
  ASSERT_NE(center, nullptr);
  EXPECT_EQ(center->getBlock(BlockPos{48, 37, 48}) >> 4, 169);
  EXPECT_EQ(center->getBlock(BlockPos{48, 38, 48}) >> 4, 9);
}

TEST(SchematicLoaderTest, CorruptedOrEmptyInputHandling) {
  std::vector<uint8_t> empty_data;
  EXPECT_FALSE(SchematicLoader::loadFromCompressedMemory(empty_data).has_value());

  std::vector<uint8_t> garbage = {0x1F, 0x8B, 0x00, 0xFF, 0xFE};
  EXPECT_FALSE(SchematicLoader::loadFromCompressedMemory(garbage).has_value());
}
