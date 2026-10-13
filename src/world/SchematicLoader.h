/**
 * @file SchematicLoader.h
 * @brief WorldEdit .schematic 经典建筑蓝图加载与解析器（Domain 领域层）
 *
 * 核心规范：
 * 1. 支持标准 NBT (Gzip/Deflate) 格式解析；
 * 2. 提取 Width, Height, Length 与 Blocks, Data (及可选 AddBlocks)；
 * 3. 严格遵循 1.12.2 全局方块状态编码 (block_id << 4 | meta)；
 * 4. 提供与 Chunk 空间包围盒求交的零浪费切片粘贴接口 (pasteToChunk)。
 */

#ifndef MCPE_WORLD_SCHEMATIC_LOADER_H
#define MCPE_WORLD_SCHEMATIC_LOADER_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

#include "common/SpatialTypes.h"
#include "world/Chunk.h"

namespace mcpe::world {

/**
 * @brief 内存中的只读建筑蓝图数据结构
 */
struct Schematic {
  int16_t width{0};   // X 轴横向尺寸
  int16_t height{0};  // Y 轴高度尺寸
  int16_t length{0};  // Z 轴纵深尺寸

  std::vector<uint8_t> blocks;      // 基础 8-bit 方块 ID (Size = width * height * length)
  std::vector<uint8_t> data;        // 4-bit 方块元数据 (Size = width * height * length)
  std::vector<uint8_t> add_blocks;  // 扩展高位 ID (可选，支持 ID > 255)

  [[nodiscard]] size_t
  totalBlocks() const noexcept {
    return static_cast<size_t>(width) * height * length;
  }

  [[nodiscard]] bool
  isValid() const noexcept {
    return width > 0 && height > 0 && length > 0 && blocks.size() == totalBlocks() &&
           data.size() == totalBlocks();
  }

  /**
   * @brief 查询蓝图局部坐标下的方块全局状态 ID
   * 格式: (block_id << 4) | (meta & 0x0F)
   */
  [[nodiscard]] uint16_t getBlockState(int32_t x, int32_t y, int32_t z) const noexcept;
};

/**
 * @brief 蓝图文件与内存解析器
 */
class SchematicLoader {
 public:
  /**
   * @brief 从指定磁盘路径加载并解析 .schematic 文件
   */
  [[nodiscard]] static std::optional<Schematic> loadFromFile(const std::filesystem::path& file_path);

  /**
   * @brief 从已解压的 NBT 二进制缓冲区解析 Schematic
   */
  [[nodiscard]] static std::optional<Schematic> loadFromNbtBuffer(
      std::span<const uint8_t> nbt_bytes
  );

  /**
   * @brief 从原始 Gzip 压缩字节流加载并解析
   */
  [[nodiscard]] static std::optional<Schematic> loadFromCompressedMemory(
      std::span<const uint8_t> compressed_bytes
  );

  /**
   * @brief 使用 zlib 解压 Gzip 缓冲区数据
   */
  [[nodiscard]] static std::optional<std::vector<uint8_t>> decompressGzip(
      std::span<const uint8_t> compressed_bytes
  );

  /**
   * @brief 将 Schematic 蓝图方块粘贴至目标 Chunk 中（基于 AABB 包围盒快速求交）
   * @param schematic 蓝图对象
   * @param origin 蓝图最小角点在世界中的放置原点 BlockPos
   * @param chunk 目标区块
   */
  static void pasteToChunk(
      const Schematic& schematic,
      const BlockPos& origin,
      Chunk& chunk
  );
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_SCHEMATIC_LOADER_H
