#include "world/Chunk.h"

#include <cassert>

namespace mcpe::world {

Chunk::Chunk(ChunkPos pos) : pos_(pos) {
  biomes_.fill(1);  // 0x01 = Plains 平原群系
}

void
Chunk::setBlock(BlockPos pos, uint16_t state_id) {
  // 防御性校验：写入的方块必须归属于本区块柱
  assert(pos.toChunkPos() == pos_ && "BlockPos does not belong to this Chunk!");

  const int32_t sec_idx = pos.sectionIndex();
  if (sec_idx < 0 || sec_idx >= static_cast<int32_t>(kSectionCount)) {
    return;
  }
  if (!sections_[sec_idx]) {
    sections_[sec_idx] = std::make_unique<ChunkSection>();
  }
  sections_[sec_idx]->setBlock(pos, state_id);
}

uint16_t
Chunk::getBlock(BlockPos pos) const {
  if (pos.toChunkPos() != pos_) {
    return BlockStates::kAir;
  }
  const int32_t sec_idx = pos.sectionIndex();
  if (sec_idx < 0 || sec_idx >= static_cast<int32_t>(kSectionCount)) {
    return BlockStates::kAir;
  }
  if (!sections_[sec_idx]) {
    return BlockStates::kAir;
  }
  return sections_[sec_idx]->getBlock(pos);
}

uint16_t
Chunk::primaryBitMask() const {
  uint16_t mask = 0;
  for (size_t i = 0; i < kSectionCount; ++i) {
    if (sections_[i] && !sections_[i]->isEmpty()) {
      mask |= static_cast<uint16_t>(1 << i);
    }
  }
  return mask;
}

const ChunkSection*
Chunk::getSection(size_t index) const {
  if (index >= kSectionCount) {
    return nullptr;
  }
  return sections_[index].get();
}

}  // namespace mcpe::world
