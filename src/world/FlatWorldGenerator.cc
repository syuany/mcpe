#include "world/FlatWorldGenerator.h"

#include "common/SpatialTypes.h"

namespace mcpe::world {

void
FlatWorldGenerator::generate(Chunk& chunk) {
  // 基于 ChunkPos 获取本区块基底绝对坐标 (Y=0)
  const BlockPos base = chunk.pos().minBlockPos();

  for (int32_t dx = 0; dx < 16; ++dx) {
    for (int32_t dz = 0; dz < 16; ++dz) {
      const BlockPos col = base + BlockPos{dx, 0, dz};
      chunk.setBlock(col, BlockStates::kBedrock);      // Y=0: 基岩
      chunk.setBlock(col.up(1), BlockStates::kDirt);   // Y=1: 泥土
      chunk.setBlock(col.up(2), BlockStates::kDirt);   // Y=2: 泥土
      chunk.setBlock(col.up(3), BlockStates::kGrass);  // Y=3: 草方块
    }
  }
}

}  // namespace mcpe::world
