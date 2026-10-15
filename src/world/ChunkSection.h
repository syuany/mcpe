#ifndef MCPE_WORLD_CHUNK_SECTION_H
#define MCPE_WORLD_CHUNK_SECTION_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>

#include "common/SpatialTypes.h"

namespace mcpe::world {

/**
 * @brief 管理 16x16x16 单段立方体方块的紧凑一维内存容器
 */
class ChunkSection {
 public:
  static constexpr size_t kSectionSize = 16;
  static constexpr size_t kBlockCount = 4096;
  static constexpr size_t kBitsPerBlock = 13;
  static constexpr size_t kDataLongs = (kBlockCount * kBitsPerBlock) / 64;  // 832

  ChunkSection();

  /** @brief 强类型 BlockPos 方块状态设置接口 */
  void setBlock(BlockPos pos, uint16_t state_id);

  /** @brief 强类型 BlockPos 方块状态查询接口 */
  [[nodiscard]] uint16_t getBlock(BlockPos pos) const;

  /** @brief 将方块数据压缩为 1.12.2 的 832 个 64 位大端整数 */
  void packToBits13(std::span<uint64_t, kDataLongs> out_longs) const;

  /** @brief 获取底层连续 4096 个方块状态数组只读引用（供预烘焙地图二进制序列化使用） */
  [[nodiscard]] const std::array<uint16_t, kBlockCount>&
  blocks() const noexcept {
    return blocks_;
  }

  /** @brief 从预烘焙二进制块批量载入 4096 个方块状态并重算非空计数 */
  void
  loadRawBlocks(std::span<const uint16_t, kBlockCount> raw_blocks) noexcept {
    std::copy(raw_blocks.begin(), raw_blocks.end(), blocks_.begin());
    non_air_count_ = 0;
    for (uint16_t state : blocks_) {
      if (state != BlockStates::kAir) {
        ++non_air_count_;
      }
    }
  }

  [[nodiscard]] size_t
  nonAirCount() const noexcept {
    return non_air_count_;
  }

  [[nodiscard]] bool
  isEmpty() const noexcept {
    return non_air_count_ == 0;
  }

 private:
  std::array<uint16_t, kBlockCount> blocks_;
  size_t non_air_count_{0};
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_CHUNK_SECTION_H
