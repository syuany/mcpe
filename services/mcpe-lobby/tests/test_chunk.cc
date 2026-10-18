#include <gtest/gtest.h>

#include <span>
#include <unordered_set>
#include <vector>

#include "common/SpatialTypes.h"
#include "protocol/ChunkSerializer.h"
#include "world/Chunk.h"
#include "world/ChunkSection.h"
#include "world/FlatWorldGenerator.h"

using namespace mcpe;
using namespace mcpe::world;
using namespace mcpe::protocol;

// 1. 负数坐标向下取整与局部坐标解算单测
TEST(WorldTypesTest, NegativeCoordinateRounding) {
  // X = -1 -> Chunk = -1, Local = 15
  BlockPos p1{-1, 0, 0};
  EXPECT_EQ(p1.toChunkPos().x, -1);
  EXPECT_EQ(p1.localX(), 15);

  // X = -16 -> Chunk = -1, Local = 0
  BlockPos p2{-16, 0, 0};
  EXPECT_EQ(p2.toChunkPos().x, -1);
  EXPECT_EQ(p2.localX(), 0);

  // X = -17 -> Chunk = -2, Local = 15
  BlockPos p3{-17, 0, 0};
  EXPECT_EQ(p3.toChunkPos().x, -2);
  EXPECT_EQ(p3.localX(), 15);

  // Z 轴测试
  BlockPos p4{0, 64, -1};
  EXPECT_EQ(p4.toChunkPos().z, -1);
  EXPECT_EQ(p4.localZ(), 15);
  EXPECT_EQ(p4.sectionIndex(), 4);
  EXPECT_EQ(p4.localSectionY(), 0);
}

// 2. 空间导航原语与数学运算符测试 (up/down/north/south/east/west, minBlockPos)
TEST(WorldTypesTest, SpatialNavigation) {
  BlockPos p{10, 20, 30};
  EXPECT_EQ(p.up(2), (BlockPos{10, 22, 30}));
  EXPECT_EQ(p.down(5), (BlockPos{10, 15, 30}));
  EXPECT_EQ(p.north(1), (BlockPos{10, 20, 29}));
  EXPECT_EQ(p.south(3), (BlockPos{10, 20, 33}));
  EXPECT_EQ(p.west(4), (BlockPos{6, 20, 30}));
  EXPECT_EQ(p.east(1), (BlockPos{11, 20, 30}));

  EXPECT_EQ((p + BlockPos{1, 2, 3}), (BlockPos{11, 22, 33}));
  EXPECT_EQ((p - BlockPos{1, 2, 3}), (BlockPos{9, 18, 27}));

  ChunkPos cp{2, -3};
  EXPECT_EQ(cp.minBlockPos(), (BlockPos{32, 0, -48}));
}

// 3. BlockPos 与 ChunkPos 运算符与哈希集合支持
TEST(WorldTypesTest, PosOperatorsAndHash) {
  ChunkPos cp1{1, -2};
  ChunkPos cp2{1, -2};
  ChunkPos cp3{2, -2};
  EXPECT_EQ(cp1, cp2);
  EXPECT_NE(cp1, cp3);
  EXPECT_TRUE(cp1 < cp3);

  std::unordered_set<ChunkPos> chunk_set;
  chunk_set.insert(cp1);
  EXPECT_TRUE(chunk_set.contains(cp2));
  EXPECT_FALSE(chunk_set.contains(cp3));

  BlockPos bp1{10, 64, -5};
  BlockPos bp2{10, 64, -5};
  BlockPos bp3{10, 65, -5};
  EXPECT_EQ(bp1, bp2);
  EXPECT_NE(bp1, bp3);
  EXPECT_TRUE(bp1 < bp3);

  std::unordered_set<BlockPos> block_set;
  block_set.insert(bp1);
  EXPECT_TRUE(block_set.contains(bp2));
  EXPECT_FALSE(block_set.contains(bp3));
}

// 4. Minecraft 1.12.2 协议 64 位 Position 位域编解码测试
TEST(WorldTypesTest, ProtocolPositionPackUnpack) {
  // 正数坐标测试
  BlockPos pos1{12345, 64, 54321};
  uint64_t packed1 = pos1.toProtocolPosition();
  BlockPos unpacked1 = BlockPos::fromProtocolPosition(packed1);
  EXPECT_EQ(unpacked1, pos1);

  // 负数与边界坐标符号扩展测试
  BlockPos pos2{-1, 0, -1};
  uint64_t packed2 = pos2.toProtocolPosition();
  BlockPos unpacked2 = BlockPos::fromProtocolPosition(packed2);
  EXPECT_EQ(unpacked2, pos2);

  BlockPos pos3{-33554432, 2047, -33554432};  // 极值边界
  uint64_t packed3 = pos3.toProtocolPosition();
  BlockPos unpacked3 = BlockPos::fromProtocolPosition(packed3);
  EXPECT_EQ(unpacked3, pos3);

  BlockPos pos4{33554431, -2048, 33554431};
  uint64_t packed4 = pos4.toProtocolPosition();
  BlockPos unpacked4 = BlockPos::fromProtocolPosition(packed4);
  EXPECT_EQ(unpacked4, pos4);
}

// 5. Chunk 纯强类型构造与 BlockPos 访问单测
TEST(ChunkTest, PureBlockPosInterfaces) {
  ChunkPos cpos{3, -5};
  Chunk chunk(cpos);
  EXPECT_EQ(chunk.pos(), cpos);
  EXPECT_EQ(chunk.x(), 3);
  EXPECT_EQ(chunk.z(), -5);

  // 写入属于本 Chunk 的绝对坐标
  BlockPos bpos = cpos.minBlockPos() + BlockPos{1, 35, 2};  // sectionIndex = 2, localSectionY = 3
  EXPECT_EQ(bpos.toChunkPos(), cpos);

  chunk.setBlock(bpos, BlockStates::kStone);
  EXPECT_EQ(chunk.getBlock(bpos), BlockStates::kStone);
  EXPECT_EQ(chunk.primaryBitMask(), (1 << 2));

  // 访问非本区块坐标防御性返回空气
  BlockPos other_chunk_pos{0, 0, 0};
  EXPECT_EQ(chunk.getBlock(other_chunk_pos), BlockStates::kAir);
}

// 6. ChunkSection 纯强类型 BlockPos 极限坐标寻址单测
TEST(ChunkSectionTest, BoundaryAddressingAndBlockPos) {
  ChunkSection section;
  EXPECT_TRUE(section.isEmpty());
  EXPECT_EQ(section.nonAirCount(), 0);

  // 通过强类型 BlockPos 设置 (0, 0, 0)
  section.setBlock(BlockPos{0, 0, 0}, BlockStates::kBedrock);
  EXPECT_FALSE(section.isEmpty());
  EXPECT_EQ(section.nonAirCount(), 1);
  EXPECT_EQ(section.getBlock(BlockPos{0, 0, 0}), BlockStates::kBedrock);

  // 设置 (15, 15, 15)
  section.setBlock(BlockPos{15, 15, 15}, BlockStates::kGrass);
  EXPECT_EQ(section.nonAirCount(), 2);
  EXPECT_EQ(section.getBlock(BlockPos{15, 15, 15}), BlockStates::kGrass);

  // 空气覆盖递减测试
  section.setBlock(BlockPos{0, 0, 0}, BlockStates::kAir);
  EXPECT_EQ(section.nonAirCount(), 1);
  EXPECT_EQ(section.getBlock(BlockPos{0, 0, 0}), BlockStates::kAir);

  section.setBlock(BlockPos{15, 15, 15}, BlockStates::kAir);
  EXPECT_TRUE(section.isEmpty());
  EXPECT_EQ(section.nonAirCount(), 0);
}

// 7. 13 位压缩打包与解包往返无损单测（覆盖跨 Long 边界的条目）
TEST(ChunkSectionTest, PackToBits13RoundTrip) {
  ChunkSection section;

  std::vector<uint16_t> expected_blocks(ChunkSection::kBlockCount);
  for (size_t i = 0; i < ChunkSection::kBlockCount; ++i) {
    uint16_t state_id = static_cast<uint16_t>((i * 7 + 13) & 0x1FFF);
    expected_blocks[i] = state_id;

    int32_t x = static_cast<int32_t>(i & 0x0F);
    int32_t z = static_cast<int32_t>((i >> 4) & 0x0F);
    int32_t y = static_cast<int32_t>((i >> 8) & 0x0F);
    section.setBlock(BlockPos{x, y, z}, state_id);
  }

  std::array<uint64_t, ChunkSection::kDataLongs> packed{};
  section.packToBits13(packed);

  // 解包验证
  for (size_t idx = 0; idx < ChunkSection::kBlockCount; ++idx) {
    size_t start_long = (idx * ChunkSection::kBitsPerBlock) / 64;
    size_t start_offset = (idx * ChunkSection::kBitsPerBlock) % 64;
    size_t end_long = ((idx + 1) * ChunkSection::kBitsPerBlock - 1) / 64;

    uint64_t val = 0;
    if (start_long == end_long) {
      val = (packed[start_long] >> start_offset) & ((1ULL << ChunkSection::kBitsPerBlock) - 1);
    } else {
      uint64_t low = packed[start_long] >> start_offset;
      uint64_t high = packed[end_long] << (64 - start_offset);
      val = (low | high) & ((1ULL << ChunkSection::kBitsPerBlock) - 1);
    }

    ASSERT_EQ(val, expected_blocks[idx]) << "Mismatch at block index " << idx;
  }
}

// 8. 超平坦生成器与 Bitmask 测试
TEST(FlatWorldGeneratorTest, GenerateSingleSection) {
  ChunkPos cpos{0, 0};
  Chunk chunk(cpos);
  EXPECT_EQ(chunk.primaryBitMask(), 0x00);

  FlatWorldGenerator generator;
  generator.generate(chunk);

  // 超平坦高度为 0~3，全在 Section 0 中，因此 BitMask 严格为 0x01
  EXPECT_EQ(chunk.primaryBitMask(), 0x01);

  // 验证方块类型
  const BlockPos base = cpos.minBlockPos();
  for (int32_t x = 0; x < 16; ++x) {
    for (int32_t z = 0; z < 16; ++z) {
      const BlockPos col = base + BlockPos{x, 0, z};
      EXPECT_EQ(chunk.getBlock(col), BlockStates::kBedrock);
      EXPECT_EQ(chunk.getBlock(col.up(1)), BlockStates::kDirt);
      EXPECT_EQ(chunk.getBlock(col.up(2)), BlockStates::kDirt);
      EXPECT_EQ(chunk.getBlock(col.up(3)), BlockStates::kGrass);
      EXPECT_EQ(chunk.getBlock(col.up(4)), BlockStates::kAir);
    }
  }
}

// 9. 序列化数据包 Payload 长度断言严格等于黄金魔数 11012 字节！
TEST(ChunkSerializerTest, SerializePayloadGoldenNumber11012) {
  Chunk chunk(ChunkPos{0, 0});
  FlatWorldGenerator generator;
  generator.generate(chunk);

  ASSERT_EQ(chunk.primaryBitMask(), 0x01);

  // ground_up = true: 包含 256 字节 Biomes，总长度严格等于 11012
  ByteBuffer payload_ground_up = ChunkSerializer::serialize(chunk, true);
  EXPECT_EQ(payload_ground_up.readableBytes(), 11012);

  // ground_up = false: 不包含 256 字节 Biomes，总长度严格等于 10756
  ByteBuffer payload_partial = ChunkSerializer::serialize(chunk, false);
  EXPECT_EQ(payload_partial.readableBytes(), 10756);
}
