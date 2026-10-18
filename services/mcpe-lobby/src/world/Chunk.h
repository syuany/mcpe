#ifndef MCPE_WORLD_CHUNK_H
#define MCPE_WORLD_CHUNK_H

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <span>

#include "common/SpatialTypes.h"
#include "world/ChunkSection.h"

namespace mcpe::world {

/**
 * @brief 管理 16x256x16 垂直体素区块柱的世界领域容器
 */
class Chunk {
 public:
  static constexpr size_t kSectionCount = 16;
  static constexpr size_t kBiomeCount = 256;

  /** @brief 纯强类型构造：由 ChunkPos 唯一标识 */
  explicit Chunk(ChunkPos pos);

  [[nodiscard]] ChunkPos
  pos() const noexcept {
    return pos_;
  }
  [[nodiscard]] int32_t
  x() const noexcept {
    return pos_.x;
  }
  [[nodiscard]] int32_t
  z() const noexcept {
    return pos_.z;
  }

  /** @brief 强类型 BlockPos 方块设置（内部严格进行区块归属防御性检查） */
  void setBlock(BlockPos pos, uint16_t state_id);

  /** @brief 强类型 BlockPos 方块查询 */
  [[nodiscard]] uint16_t getBlock(BlockPos pos) const;

  /** @brief 计算 16 位的掩码：哪些 Section 存在且非空 */
  [[nodiscard]] uint16_t primaryBitMask() const;

  /** @brief 获取特定索引的 Section 指针（可能为 nullptr） */
  [[nodiscard]] const ChunkSection* getSection(size_t index) const;

  /** @brief 获取或创建指定索引的 Section（供预烘焙地图反序列化填充使用） */
  ChunkSection&
  getOrCreateSection(size_t index) {
    assert(index < kSectionCount);
    if (!sections_[index]) {
      sections_[index] = std::make_unique<ChunkSection>();
    }
    return *sections_[index];
  }

  /** @brief 获取生物群系数组只读引用 */
  [[nodiscard]] const std::array<uint8_t, kBiomeCount>&
  biomes() const noexcept {
    return biomes_;
  }

  /** @brief 批量设置生物群系数据 */
  void
  setBiomes(std::span<const uint8_t, kBiomeCount> biomes) noexcept {
    std::copy(biomes.begin(), biomes.end(), biomes_.begin());
  }

 private:
  ChunkPos pos_{0, 0};
  std::array<std::unique_ptr<ChunkSection>, kSectionCount> sections_;
  std::array<uint8_t, kBiomeCount> biomes_;
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_CHUNK_H
