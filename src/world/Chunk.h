#ifndef MCPE_WORLD_CHUNK_H
#define MCPE_WORLD_CHUNK_H

#include <array>
#include <cstdint>
#include <memory>

#include "world/ChunkSection.h"
#include "world/WorldTypes.h"

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

  /** @brief 获取生物群系数组只读引用 */
  [[nodiscard]] const std::array<uint8_t, kBiomeCount>&
  biomes() const noexcept {
    return biomes_;
  }

 private:
  ChunkPos pos_{0, 0};
  std::array<std::unique_ptr<ChunkSection>, kSectionCount> sections_;
  std::array<uint8_t, kBiomeCount> biomes_;
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_CHUNK_H
