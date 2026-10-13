/**
 * @file test_schematic.cc
 * @brief SchematicLoader 建筑蓝图解析与区块粘贴单元测试
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

#include "common/SpatialTypes.h"
#include "world/Chunk.h"
#include "world/SchematicLoader.h"

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
  // 中心喷泉顶部是水 (Block ID 9)
  const uint16_t water_state = schem.getBlockState(48, 38, 48);
  EXPECT_EQ(water_state >> 4, 9);  // Water

  // 中心喷泉主水柱顶端是海晶灯 (Block ID 169)
  const uint16_t sea_lantern_state = schem.getBlockState(48, 37, 48);
  EXPECT_EQ(sea_lantern_state >> 4, 169);  // Sea Lantern

  // 2. 验证四个方位的传送门黑曜石门框 (Block ID 49)
  // 北向传送门位于 px = 48, pz = 48 - 18 = 30, surface_y = 32
  const uint16_t obsidian_state = schem.getBlockState(47, 33, 30);
  EXPECT_EQ(obsidian_state >> 4, 49);  // Obsidian

  // 3. 验证外围虚空区域 (0, 0, 0) 是空气 (Block ID 0)
  EXPECT_EQ(schem.getBlockState(0, 0, 0), 0);
  // 越界坐标安全返回空气
  EXPECT_EQ(schem.getBlockState(-1, 0, 0), 0);
  EXPECT_EQ(schem.getBlockState(100, 100, 100), 0);
}

TEST(SchematicLoaderTest, PasteToChunkBoundingBoxIntersection) {
  const std::filesystem::path schem_path = findLobbySchematic();
  auto schem_opt = SchematicLoader::loadFromFile(schem_path);
  ASSERT_TRUE(schem_opt.has_value());
  const auto& schem = *schem_opt;

  // 将蓝图放置于世界以 (0, 0, 0) 为原点
  // ChunkPos(3, 3) 涵盖的世界坐标范围是 X: [48, 63], Z: [48, 63]（正好包含中心喷泉）
  Chunk center_chunk(ChunkPos{3, 3});
  SchematicLoader::pasteToChunk(schem, BlockPos{0, 0, 0}, center_chunk);

  // 验证 Chunk 中的方块状态与 Schematic 蓝图在交集点 (48, 37, 48) 一致
  const uint16_t state = center_chunk.getBlock(BlockPos{48, 37, 48});
  EXPECT_EQ(state >> 4, 169);  // Sea Lantern

  // 验证不相交的远端 Chunk，调用 pasteToChunk 安全无异常
  Chunk far_chunk(ChunkPos{100, 100});
  SchematicLoader::pasteToChunk(schem, BlockPos{0, 0, 0}, far_chunk);
  EXPECT_TRUE(far_chunk.primaryBitMask() == 0);
}

TEST(SchematicLoaderTest, CorruptedOrEmptyInputHandling) {
  // 空数据
  std::vector<uint8_t> empty_data;
  EXPECT_FALSE(SchematicLoader::loadFromCompressedMemory(empty_data).has_value());

  // 损坏的假数据
  std::vector<uint8_t> garbage = {0x1F, 0x8B, 0x00, 0xFF, 0xFE};
  EXPECT_FALSE(SchematicLoader::loadFromCompressedMemory(garbage).has_value());
}
