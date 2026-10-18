/**
 * @file SpatialTypes.h
 * @brief 全局空间几何、离散体素坐标与视角原语定义（Foundations 基础层）
 *
 * 作为全工程共享的叶子值类型（Leaf Value Types），纯数学与类型封装，
 * 供 Network, Protocol, World, Entity, Game 各层无障碍复用，杜绝散装 double/float 基础类型偏执。
 */

#ifndef MCPE_COMMON_SPATIAL_TYPES_H
#define MCPE_COMMON_SPATIAL_TYPES_H

#include <cmath>
#include <compare>
#include <cstdint>
#include <functional>

namespace mcpe {

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

inline constexpr BlockPos
ChunkPos::minBlockPos() const noexcept {
  return BlockPos{x << 4, 0, z << 4};
}

/**
 * @brief 三维连续空间几何向量 (X, Y, Z) - 用于实体连续物理坐标、位移计算与碰撞
 */
struct Vector3d {
  double x{0.0};
  double y{0.0};
  double z{0.0};

  constexpr auto operator<=>(const Vector3d&) const = default;

  constexpr Vector3d
  operator+(const Vector3d& other) const noexcept {
    return {x + other.x, y + other.y, z + other.z};
  }

  constexpr Vector3d
  operator-(const Vector3d& other) const noexcept {
    return {x - other.x, y - other.y, z - other.z};
  }

  [[nodiscard]] double
  distanceSquared(const Vector3d& other) const noexcept {
    const double dx = x - other.x;
    const double dy = y - other.y;
    const double dz = z - other.z;
    return dx * dx + dy * dy + dz * dz;
  }

  [[nodiscard]] double
  distance(const Vector3d& other) const noexcept {
    return std::sqrt(distanceSquared(other));
  }

  [[nodiscard]] double
  distanceSquared(const BlockPos& block) const noexcept {
    const double dx = x - (static_cast<double>(block.x) + 0.5);
    const double dy = y - (static_cast<double>(block.y) + 0.5);
    const double dz = z - (static_cast<double>(block.z) + 0.5);
    return dx * dx + dy * dy + dz * dz;
  }

  [[nodiscard]] BlockPos
  toBlockPos() const noexcept {
    return BlockPos{
        static_cast<int32_t>(std::floor(x)),
        static_cast<int32_t>(std::floor(y)),
        static_cast<int32_t>(std::floor(z))};
  }
};

/**
 * @brief 三维轴对齐包围盒 (Axis-Aligned Bounding Box) - 用于传送门与空间碰撞检测
 */
struct AABB {
  Vector3d min_pt{};
  Vector3d max_pt{};

  constexpr auto operator<=>(const AABB&) const = default;

  [[nodiscard]] constexpr bool
  contains(const Vector3d& p) const noexcept {
    const double min_x = (min_pt.x <= max_pt.x) ? min_pt.x : max_pt.x;
    const double max_x = (min_pt.x <= max_pt.x) ? max_pt.x : min_pt.x;
    const double min_y = (min_pt.y <= max_pt.y) ? min_pt.y : max_pt.y;
    const double max_y = (min_pt.y <= max_pt.y) ? max_pt.y : min_pt.y;
    const double min_z = (min_pt.z <= max_pt.z) ? min_pt.z : max_pt.z;
    const double max_z = (min_pt.z <= max_pt.z) ? max_pt.z : min_pt.z;

    return (p.x >= min_x && p.x <= max_x) &&
           (p.y >= min_y && p.y <= max_y) &&
           (p.z >= min_z && p.z <= max_z);
  }
};

/**
 * @brief 实体视角朝向原语 (Yaw, Pitch)
 */
struct Rotation {
  float yaw{0.0f};
  float pitch{0.0f};

  constexpr auto operator<=>(const Rotation&) const = default;

  /** @brief 转换为 Minecraft 协议 1 字节定步长 Angle (0~255) */
  [[nodiscard]] uint8_t
  toByteYaw() const noexcept {
    return static_cast<uint8_t>(static_cast<int32_t>(std::floor((yaw / 360.0f) * 256.0f)));
  }

  [[nodiscard]] uint8_t
  toBytePitch() const noexcept {
    return static_cast<uint8_t>(static_cast<int32_t>(std::floor((pitch / 360.0f) * 256.0f)));
  }
};

/**
 * @brief 方块接触表面强类型枚举（对标 Minecraft 0x14/0x1F Face 字段）
 */
enum class BlockFace : uint8_t {
  Bottom = 0,  // -Y (下)
  Top = 1,     // +Y (上)
  North = 2,   // -Z (北)
  South = 3,   // +Z (南)
  West = 4,    // -X (西)
  East = 5     // +X (东)
};

/** @brief 获取指定面沿法向的坐标偏移单位量 */
[[nodiscard]] constexpr BlockPos
toFaceOffset(BlockFace face) noexcept {
  switch (face) {
    case BlockFace::Bottom:
      return BlockPos{0, -1, 0};
    case BlockFace::Top:
      return BlockPos{0, 1, 0};
    case BlockFace::North:
      return BlockPos{0, 0, -1};
    case BlockFace::South:
      return BlockPos{0, 0, 1};
    case BlockFace::West:
      return BlockPos{-1, 0, 0};
    case BlockFace::East:
      return BlockPos{1, 0, 0};
  }
  return BlockPos{0, 0, 0};
}

/**
 * @brief 客户端方块挖掘状态机枚举（对标 0x14 Player Digging 状态）
 */
enum class DigStatus : uint8_t {
  Started = 0,        // 开始破坏方块
  Cancelled = 1,      // 放弃破坏方块
  Finished = 2,       // 成功破坏方块
  DropItemStack = 3,  // 丢弃整组物品
  DropItem = 4,       // 丢弃单个物品
  ShootOrFinish = 5,  // 射箭 / 吃完食物
  SwapHands = 6       // 切换主副手物品
};

/** @brief 方块状态转换工具：消除裸数字魔数 */
constexpr uint16_t
toGlobalState(uint16_t block_id, uint8_t meta = 0) noexcept {
  return static_cast<uint16_t>((block_id << 4) | (meta & 0x0F));
}

namespace world {
using ChunkPos = mcpe::ChunkPos;
using BlockPos = mcpe::BlockPos;
using Vector3d = mcpe::Vector3d;
using AABB = mcpe::AABB;
using Rotation = mcpe::Rotation;
using BlockFace = mcpe::BlockFace;
using DigStatus = mcpe::DigStatus;

using mcpe::toFaceOffset;
using mcpe::toGlobalState;

namespace BlockStates {
inline constexpr uint16_t kAir = toGlobalState(0, 0);
inline constexpr uint16_t kStone = toGlobalState(1, 0);
inline constexpr uint16_t kGrass = toGlobalState(2, 0);
inline constexpr uint16_t kDirt = toGlobalState(3, 0);
inline constexpr uint16_t kBedrock = toGlobalState(7, 0);
}  // namespace BlockStates

}  // namespace world

}  // namespace mcpe

template <>
struct std::hash<mcpe::ChunkPos> {
  size_t
  operator()(const mcpe::ChunkPos& pos) const noexcept {
    return (static_cast<size_t>(pos.x) * 3129871) ^ static_cast<size_t>(pos.z);
  }
};

template <>
struct std::hash<mcpe::BlockPos> {
  size_t
  operator()(const mcpe::BlockPos& pos) const noexcept {
    return ((static_cast<size_t>(pos.x) * 8976890) ^ (static_cast<size_t>(pos.y) * 981131)) ^
           static_cast<size_t>(pos.z);
  }
};

#endif  // MCPE_COMMON_SPATIAL_TYPES_H
