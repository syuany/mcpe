#include "world/ChunkSection.h"

#include <algorithm>

namespace mcpe::world {

ChunkSection::ChunkSection() {
  blocks_.fill(BlockStates::kAir);
  non_air_count_ = 0;
}

void
ChunkSection::setBlock(BlockPos pos, uint16_t state_id) {
  const size_t idx = pos.sectionBlockIndex();
  const bool was_air = (blocks_[idx] == BlockStates::kAir);
  const bool is_air = (state_id == BlockStates::kAir);

  if (was_air && !is_air) {
    ++non_air_count_;
  } else if (!was_air && is_air) {
    --non_air_count_;
  }
  blocks_[idx] = state_id;
}

uint16_t
ChunkSection::getBlock(BlockPos pos) const {
  return blocks_[pos.sectionBlockIndex()];
}

void
ChunkSection::packToBits13(std::span<uint64_t, kDataLongs> out_longs) const {
  std::ranges::fill(out_longs, 0);

  for (size_t idx = 0; idx < kBlockCount; ++idx) {
    const uint64_t val = static_cast<uint64_t>(blocks_[idx]) & ((1ULL << kBitsPerBlock) - 1);
    const size_t start_long = (idx * kBitsPerBlock) / 64;
    const size_t start_offset = (idx * kBitsPerBlock) % 64;
    const size_t end_long = ((idx + 1) * kBitsPerBlock - 1) / 64;

    out_longs[start_long] |= (val << start_offset);
    if (start_long != end_long) {
      out_longs[end_long] |= (val >> (64 - start_offset));
    }
  }
}

}  // namespace mcpe::world
