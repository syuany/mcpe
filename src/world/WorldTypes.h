#ifndef MCPE_WORLD_WORLD_TYPES_H
#define MCPE_WORLD_WORLD_TYPES_H

#include <compare>
#include <cstdint>
#include <functional>

namespace mcpe::world {

struct BlockPos;

/**
 * @brief 区块二维空间坐标原语 (X, Z)
 */
struct ChunkPos {
  int32_t x{0};
  int32_t z{0};

  constexpr auto operator<=>(const ChunkPos&) const = default;

  /** @brief 获取当前区块在世界坐标系下的最小原点方块坐标 (Y=0) */
  [[nodiscard]] constexpr BlockPos minBlockPos() const noexcept;
};

/**
 * @brief 三维体素离散空间坐标原语 (X, Y, Z)
 */
struct BlockPos {
  int32_t x{0};
  int32_t y{0};
  int32_t z{0};

  constexpr auto operator<=>(const BlockPos&) const = default;

  constexpr BlockPos
  operator+(const BlockPos& other) const noexcept {
    return BlockPos{x + other.x, y + other.y, z + other.z};
  }

  constexpr BlockPos
  operator-(const BlockPos& other) const noexcept {
    return BlockPos{x - other.x, y - other.y, z - other.z};
  }

  [[nodiscard]] constexpr BlockPos
  up(int32_t amount = 1) const noexcept {
    return BlockPos{x, y + amount, z};
  }

  [[nodiscard]] constexpr BlockPos
  down(int32_t amount = 1) const noexcept {
    return BlockPos{x, y - amount, z};
  }

  [[nodiscard]] constexpr BlockPos
  north(int32_t amount = 1) const noexcept {
    return BlockPos{x, y, z - amount};
  }

  [[nodiscard]] constexpr BlockPos
  south(int32_t amount = 1) const noexcept {
    return BlockPos{x, y, z + amount};
  }

  [[nodiscard]] constexpr BlockPos
  west(int32_t amount = 1) const noexcept {
    return BlockPos{x - amount, y, z};
  }

  [[nodiscard]] constexpr BlockPos
  east(int32_t amount = 1) const noexcept {
    return BlockPos{x + amount, y, z};
  }

  /** @brief 计算该方块所属的区块坐标（算术右移，负数自然向下取整） */
  [[nodiscard]] constexpr ChunkPos
  toChunkPos() const noexcept {
    return ChunkPos{x >> 4, z >> 4};
  }

  /** @brief 区块内 X 局部坐标 [0, 15] */
  [[nodiscard]] constexpr int32_t
  localX() const noexcept {
    return x & 0x0F;
  }

  /** @brief 世界高度 Y 坐标 */
  [[nodiscard]] constexpr int32_t
  localY() const noexcept {
    return y;
  }

  /** @brief 区块内 Z 局部坐标 [0, 15] */
  [[nodiscard]] constexpr int32_t
  localZ() const noexcept {
    return z & 0x0F;
  }

  /** @brief 所属 ChunkSection 垂直段编号 [0, 15]（Y 轴每 16 格为一个 Section） */
  [[nodiscard]] constexpr int32_t
  sectionIndex() const noexcept {
    return y >> 4;
  }

  /** @brief 单段 Section 内的 Y 局部坐标 [0, 15] */
  [[nodiscard]] constexpr int32_t
  localSectionY() const noexcept {
    return y & 0x0F;
  }

  /** @brief 单段 Section 内部空间局部性寻址索引：(y & 15) << 8 | (z & 15) << 4 | (x & 15) */
  [[nodiscard]] constexpr size_t
  sectionBlockIndex() const noexcept {
    return (static_cast<size_t>(y & 0x0F) << 8) | (static_cast<size_t>(z & 0x0F) << 4) |
           static_cast<size_t>(x & 0x0F);
  }

  /**
   * @brief 按照 Minecraft 1.12.2 (Protocol 340) 打包为 64 位 Position 位域
   * 格式: ((x & 0x3FFFFFF) << 38) | ((y & 0xFFF) << 26) | (z & 0x3FFFFFF)
   */
  [[nodiscard]] constexpr uint64_t
  toProtocolPosition() const noexcept {
    return ((static_cast<uint64_t>(x) & 0x3FFFFFFULL) << 38) |
           ((static_cast<uint64_t>(y) & 0xFFFULL) << 26) |
           (static_cast<uint64_t>(z) & 0x3FFFFFFULL);
  }

  /**
   * @brief 从 Minecraft 1.12.2 协议 64 位 Position 位域解码并进行带符号扩展
   */
  [[nodiscard]] static constexpr BlockPos
  fromProtocolPosition(uint64_t val) noexcept {
    const int64_t sval = static_cast<int64_t>(val);
    const int32_t px = static_cast<int32_t>(sval >> 38);
    const int32_t py = static_cast<int32_t>((sval << 26) >> 52);
    const int32_t pz = static_cast<int32_t>((sval << 38) >> 38);
    return BlockPos{px, py, pz};
  }
};

constexpr BlockPos
ChunkPos::minBlockPos() const noexcept {
  return BlockPos{x << 4, 0, z << 4};
}

/** @brief 方块状态转换工具：消除裸数字魔数 */
constexpr uint16_t
toGlobalState(uint16_t block_id, uint8_t meta = 0) noexcept {
  return static_cast<uint16_t>((block_id << 4) | (meta & 0x0F));
}

namespace BlockStates {
inline constexpr uint16_t kAir = toGlobalState(0, 0);
inline constexpr uint16_t kStone = toGlobalState(1, 0);
inline constexpr uint16_t kGrass = toGlobalState(2, 0);
inline constexpr uint16_t kDirt = toGlobalState(3, 0);
inline constexpr uint16_t kBedrock = toGlobalState(7, 0);
}  // namespace BlockStates

}  // namespace mcpe::world

template <>
struct std::hash<mcpe::world::ChunkPos> {
  size_t
  operator()(const mcpe::world::ChunkPos& pos) const noexcept {
    return (static_cast<size_t>(pos.x) * 3129871) ^ static_cast<size_t>(pos.z);
  }
};

template <>
struct std::hash<mcpe::world::BlockPos> {
  size_t
  operator()(const mcpe::world::BlockPos& pos) const noexcept {
    return ((static_cast<size_t>(pos.x) * 8976890) ^ (static_cast<size_t>(pos.y) * 981131)) ^
           static_cast<size_t>(pos.z);
  }
};

#endif  // MCPE_WORLD_WORLD_TYPES_H
