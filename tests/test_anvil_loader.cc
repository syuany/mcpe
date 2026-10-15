/**
 * @file test_anvil_loader.cc
 * @brief AnvilLoader 原版世界存档 (.mca / .zip / level.dat) 加载单元测试
 */

#include <gtest/gtest.h>
#include <zlib.h>

#include <cstring>
#include <filesystem>
#include <vector>

#include "AnvilLoader.h"
#include "world/World.h"

using namespace mcpe;
using namespace mcpe::world;

namespace {

// 辅助函数：使用 Gzip 压缩数据 (windowBits = 15 + 16)
std::vector<uint8_t> compressGzip(std::span<const uint8_t> input) {
  z_stream strm{};
  deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  std::vector<uint8_t> out(input.size() + 128);
  strm.next_in = const_cast<Bytef*>(input.data());
  strm.avail_in = input.size();
  strm.next_out = out.data();
  strm.avail_out = out.size();
  deflate(&strm, Z_FINISH);
  out.resize(strm.total_out);
  deflateEnd(&strm);
  return out;
}

// 辅助函数：使用标准 Zlib 压缩数据 (windowBits = 15)
std::vector<uint8_t> compressZlib(std::span<const uint8_t> input) {
  z_stream strm{};
  deflateInit(&strm, Z_DEFAULT_COMPRESSION);
  std::vector<uint8_t> out(input.size() + 128);
  strm.next_in = const_cast<Bytef*>(input.data());
  strm.avail_in = input.size();
  strm.next_out = out.data();
  strm.avail_out = out.size();
  deflate(&strm, Z_FINISH);
  out.resize(strm.total_out);
  deflateEnd(&strm);
  return out;
}

void writeU8(std::vector<uint8_t>& buf, uint8_t v) { buf.push_back(v); }
void writeI16BE(std::vector<uint8_t>& buf, int16_t v) {
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
}
void writeI32BE(std::vector<uint8_t>& buf, int32_t v) {
  buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>(v & 0xFF));
}
void writeString(std::vector<uint8_t>& buf, std::string_view str) {
  writeI16BE(buf, static_cast<int16_t>(str.size()));
  buf.insert(buf.end(), str.begin(), str.end());
}

}  // namespace

TEST(AnvilLoaderTest, ParseLevelDatMetadata) {
  // 构造标准 level.dat NBT 结构:
  // Root Compound -> "Data" Compound -> SpawnX(100), SpawnY(64), SpawnZ(200), LevelName("Parkour101")
  std::vector<uint8_t> raw_nbt;
  // 1. Root Compound header
  writeU8(raw_nbt, 10);  // TAG_Compound
  writeString(raw_nbt, "");

  // 2. Data Compound header
  writeU8(raw_nbt, 10);  // TAG_Compound
  writeString(raw_nbt, "Data");

  // 2.1 SpawnX
  writeU8(raw_nbt, 3);   // TAG_Int
  writeString(raw_nbt, "SpawnX");
  writeI32BE(raw_nbt, 100);

  // 2.2 SpawnY
  writeU8(raw_nbt, 3);   // TAG_Int
  writeString(raw_nbt, "SpawnY");
  writeI32BE(raw_nbt, 64);

  // 2.3 SpawnZ
  writeU8(raw_nbt, 3);   // TAG_Int
  writeString(raw_nbt, "SpawnZ");
  writeI32BE(raw_nbt, 200);

  // 2.4 LevelName
  writeU8(raw_nbt, 8);   // TAG_String
  writeString(raw_nbt, "LevelName");
  writeString(raw_nbt, "Parkour101");

  // Data TAG_End
  writeU8(raw_nbt, 0);

  // Root TAG_End
  writeU8(raw_nbt, 0);

  // Gzip 压缩
  auto gzip_data = compressGzip(raw_nbt);

  auto meta_opt = AnvilLoader::parseLevelDat(gzip_data);
  ASSERT_TRUE(meta_opt.has_value());
  EXPECT_EQ(meta_opt->level_name, "Parkour101");
  EXPECT_DOUBLE_EQ(meta_opt->spawn_point.x, 100.5);
  EXPECT_DOUBLE_EQ(meta_opt->spawn_point.y, 64.0);
  EXPECT_DOUBLE_EQ(meta_opt->spawn_point.z, 200.5);
}

TEST(AnvilLoaderTest, ParseRegionFileChunkSections) {
  // 构造单个 Chunk NBT
  std::vector<uint8_t> chunk_nbt;
  writeU8(chunk_nbt, 10);  // Root Compound
  writeString(chunk_nbt, "");

  writeU8(chunk_nbt, 10);  // Level Compound
  writeString(chunk_nbt, "Level");

  writeU8(chunk_nbt, 3);   // xPos = 2
  writeString(chunk_nbt, "xPos");
  writeI32BE(chunk_nbt, 2);

  writeU8(chunk_nbt, 3);   // zPos = 3
  writeString(chunk_nbt, "zPos");
  writeI32BE(chunk_nbt, 3);

  writeU8(chunk_nbt, 9);   // Sections (TAG_List)
  writeString(chunk_nbt, "Sections");
  writeU8(chunk_nbt, 10);  // List of TAG_Compound
  writeI32BE(chunk_nbt, 1); // 1 Section

  // Section 0
  writeU8(chunk_nbt, 1);   // Y = 0
  writeString(chunk_nbt, "Y");
  writeU8(chunk_nbt, 0);

  writeU8(chunk_nbt, 7);   // Blocks = 4096 bytes
  writeString(chunk_nbt, "Blocks");
  writeI32BE(chunk_nbt, 4096);
  std::vector<uint8_t> blocks(4096, 0);
  blocks[0] = 1;           // (0,0,0) 设置为 Stone (ID 1)
  blocks[1] = 2;           // (1,0,0) 设置为 Grass (ID 2)
  chunk_nbt.insert(chunk_nbt.end(), blocks.begin(), blocks.end());

  writeU8(chunk_nbt, 7);   // Data = 2048 bytes
  writeString(chunk_nbt, "Data");
  writeI32BE(chunk_nbt, 2048);
  std::vector<uint8_t> data(2048, 0);
  chunk_nbt.insert(chunk_nbt.end(), data.begin(), data.end());

  writeU8(chunk_nbt, 0);   // Section TAG_End

  writeU8(chunk_nbt, 0);   // Level TAG_End
  writeU8(chunk_nbt, 0);   // Root TAG_End

  auto comp_chunk = compressZlib(chunk_nbt);

  // 构造标准 .mca 扇区文件
  // 头两个扇区为 4096 字节位置表 + 4096 字节时间戳表 = 8192 字节
  // 槽位 0 对应 (0, 0)，放入扇区 2
  std::vector<uint8_t> mca(8192, 0);

  // 槽位 0: sector 2 (offset 2 << 8), sector count 1
  const uint32_t entry_val = (2 << 8) | 1;
  mca[0] = static_cast<uint8_t>((entry_val >> 24) & 0xFF);
  mca[1] = static_cast<uint8_t>((entry_val >> 16) & 0xFF);
  mca[2] = static_cast<uint8_t>((entry_val >> 8) & 0xFF);
  mca[3] = static_cast<uint8_t>(entry_val & 0xFF);

  // Sector 2 (起始偏移 8192)
  mca.resize(8192 + 4096, 0);
  const uint32_t payload_len = static_cast<uint32_t>(comp_chunk.size() + 1);
  mca[8192] = static_cast<uint8_t>((payload_len >> 24) & 0xFF);
  mca[8193] = static_cast<uint8_t>((payload_len >> 16) & 0xFF);
  mca[8194] = static_cast<uint8_t>((payload_len >> 8) & 0xFF);
  mca[8195] = static_cast<uint8_t>(payload_len & 0xFF);
  mca[8196] = 2;  // zlib 压缩标识

  std::memcpy(mca.data() + 8197, comp_chunk.data(), comp_chunk.size());

  World world;
  size_t loaded = AnvilLoader::parseRegionFile(mca, world);
  EXPECT_EQ(loaded, 1);

  // 检查 ChunkPos(2, 3) 是否成功创建
  EXPECT_TRUE(world.hasChunk(ChunkPos{2, 3}));
  const Chunk* chk = world.getChunk(ChunkPos{2, 3});
  ASSERT_NE(chk, nullptr);

  // (cx=2, cz=3) 对应世界方块原点 (32, 0, 48)
  EXPECT_EQ(chk->getBlock(BlockPos{32, 0, 48}) >> 4, 1);  // Stone
  EXPECT_EQ(chk->getBlock(BlockPos{33, 0, 48}) >> 4, 2);  // Grass
}

TEST(AnvilLoaderTest, LoadFromDiskZipIfAvailable) {
  const std::filesystem::path paths[] = {
      "assets/maps/lobby/floating-island.zip",
      "assets/maps/lobby/world.zip",
      "../assets/maps/lobby/floating-island.zip",
      "../assets/maps/lobby/world.zip",
      "../../assets/maps/lobby/floating-island.zip",
      "../../assets/maps/lobby/world.zip"
  };

  for (const auto& p : paths) {
    if (std::filesystem::exists(p)) {
      World world;
      size_t count = AnvilLoader::loadFromZip(p, world);
      EXPECT_GT(count, 0);
      EXPECT_GT(world.chunkCount(), 0);
      // 验证出生点被自适应修正至大厅安全地面（Y=56.0，非虚空 Y=4.0）
      EXPECT_GE(world.spawnPoint().y, 50.0);
      EXPECT_LT(world.spawnPoint().x, 0.0);
      break;
    }
  }
}
