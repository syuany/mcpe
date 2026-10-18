/**
 * @file SchematicLoader.cc
 * @brief WorldEdit .schematic 经典建筑蓝图加载与解析器实现（复用统一 NbtReader，消除 175 行重复代码）
 */

#include "SchematicLoader.h"

#include <spdlog/spdlog.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string_view>

#include "NbtReader.h"

namespace mcpe::world {

namespace {

using common::NbtReader;
using common::NbtTag;

bool
parseSchematicCompound(NbtReader& reader, Schematic& out_schematic) {
  while (true) {
    auto tag_type_opt = reader.readU8();
    if (!tag_type_opt) return false;
    auto tag_type = static_cast<NbtTag>(*tag_type_opt);
    if (tag_type == NbtTag::End) {
      break;
    }

    auto name_opt = reader.readString();
    if (!name_opt) return false;
    std::string_view name = *name_opt;

    if (name == "Width" && tag_type == NbtTag::Short) {
      auto w = reader.readI16();
      if (!w || *w <= 0) return false;
      out_schematic.width = *w;
    } else if (name == "Height" && tag_type == NbtTag::Short) {
      auto h = reader.readI16();
      if (!h || *h <= 0) return false;
      out_schematic.height = *h;
    } else if (name == "Length" && tag_type == NbtTag::Short) {
      auto l = reader.readI16();
      if (!l || *l <= 0) return false;
      out_schematic.length = *l;
    } else if (name == "Blocks" && tag_type == NbtTag::ByteArray) {
      auto bytes_opt = reader.readByteArray();
      if (!bytes_opt) return false;
      out_schematic.blocks.assign(bytes_opt->begin(), bytes_opt->end());
    } else if (name == "Data" && tag_type == NbtTag::ByteArray) {
      auto bytes_opt = reader.readByteArray();
      if (!bytes_opt) return false;
      out_schematic.data.assign(bytes_opt->begin(), bytes_opt->end());
    } else if (name == "AddBlocks" && tag_type == NbtTag::ByteArray) {
      auto bytes_opt = reader.readByteArray();
      if (!bytes_opt) return false;
      out_schematic.add_blocks.assign(bytes_opt->begin(), bytes_opt->end());
    } else {
      if (!reader.skipTag(tag_type)) {
        return false;
      }
    }
  }

  return out_schematic.isValid();
}

}  // namespace

uint16_t
Schematic::getBlockState(int32_t x, int32_t y, int32_t z) const noexcept {
  if (x < 0 || x >= width || y < 0 || y >= height || z < 0 || z >= length) {
    return 0;
  }

  const size_t index = (static_cast<size_t>(y) * length + z) * width + x;
  if (index >= blocks.size()) {
    return 0;
  }

  uint16_t block_id = blocks[index];
  if (!add_blocks.empty() && (index / 2) < add_blocks.size()) {
    const uint8_t add = add_blocks[index / 2];
    const uint16_t high = (index % 2 == 0) ? (add & 0x0F) : ((add >> 4) & 0x0F);
    block_id |= (high << 8);
  }

  uint8_t meta = 0;
  if (index < data.size()) {
    meta = data[index] & 0x0F;
  }

  return toGlobalState(block_id, meta);
}

std::optional<std::vector<uint8_t>>
SchematicLoader::decompressGzip(std::span<const uint8_t> compressed_bytes) {
  if (compressed_bytes.empty()) {
    return std::nullopt;
  }

  z_stream strm{};
  // 16 + MAX_WBITS 显式启用 Gzip 自动解压头与校验
  if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
    return std::nullopt;
  }

  std::vector<uint8_t> out;
  out.resize(std::max<size_t>(compressed_bytes.size() * 4, 65536));

  strm.next_in = const_cast<Bytef*>(compressed_bytes.data());
  strm.avail_in = static_cast<uInt>(compressed_bytes.size());

  int ret = Z_OK;
  size_t total_out = 0;

  while (ret != Z_STREAM_END) {
    if (total_out == out.size()) {
      out.resize(out.size() * 2);
    }
    strm.next_out = reinterpret_cast<Bytef*>(out.data() + total_out);
    strm.avail_out = static_cast<uInt>(out.size() - total_out);

    ret = inflate(&strm, Z_NO_FLUSH);
    if (ret != Z_OK && ret != Z_STREAM_END) {
      inflateEnd(&strm);
      return std::nullopt;
    }
    total_out = strm.total_out;
  }

  inflateEnd(&strm);
  out.resize(total_out);
  return out;
}

std::optional<Schematic>
SchematicLoader::loadFromNbtBuffer(std::span<const uint8_t> nbt_bytes) {
  NbtReader reader(nbt_bytes);

  auto root_tag_type = reader.readU8();
  if (!root_tag_type || static_cast<NbtTag>(*root_tag_type) != NbtTag::Compound) {
    return std::nullopt;
  }

  auto root_name = reader.readString();
  if (!root_name) {
    return std::nullopt;
  }

  Schematic schem;
  if (!parseSchematicCompound(reader, schem)) {
    return std::nullopt;
  }

  return schem;
}

std::optional<Schematic>
SchematicLoader::loadFromCompressedMemory(std::span<const uint8_t> compressed_bytes) {
  // Gzip 魔数探测 (0x1F, 0x8B)
  if (compressed_bytes.size() >= 2 && compressed_bytes[0] == 0x1F &&
      compressed_bytes[1] == 0x8B) {
    auto decompressed = decompressGzip(compressed_bytes);
    if (!decompressed) {
      spdlog::error("[SchematicLoader] Failed to decompress Gzip data");
      return std::nullopt;
    }
    return loadFromNbtBuffer(*decompressed);
  }

  // 纯未压缩 NBT 缓冲区
  return loadFromNbtBuffer(compressed_bytes);
}

std::optional<Schematic>
SchematicLoader::loadFromFile(const std::filesystem::path& file_path) {
  std::ifstream file(file_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    spdlog::error("[SchematicLoader] Unable to open schematic file: {}", file_path.string());
    return std::nullopt;
  }

  const auto file_size = file.tellg();
  if (file_size <= 0) {
    spdlog::error("[SchematicLoader] Schematic file is empty: {}", file_path.string());
    return std::nullopt;
  }

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
  if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
    spdlog::error("[SchematicLoader] Failed to read file bytes: {}", file_path.string());
    return std::nullopt;
  }

  auto schem = loadFromCompressedMemory(buffer);
  if (schem) {
    spdlog::info(
        "[SchematicLoader] Successfully loaded '{}' (Dimensions: {}x{}x{}, Total blocks: {})",
        file_path.filename().string(),
        schem->width,
        schem->height,
        schem->length,
        schem->totalBlocks()
    );
  }
  return schem;
}

void
SchematicLoader::pasteToChunk(
    const Schematic& schematic,
    const BlockPos& origin,
    Chunk& chunk
) {
  // 1. 计算 Chunk 与 Schematic 世界包围盒交集
  const int32_t chunk_min_x = chunk.x() << 4;
  const int32_t chunk_max_x = chunk_min_x + 15;
  const int32_t chunk_min_z = chunk.z() << 4;
  const int32_t chunk_max_z = chunk_min_z + 15;

  const int32_t schem_min_x = origin.x;
  const int32_t schem_max_x = origin.x + schematic.width - 1;
  const int32_t schem_min_y = origin.y;
  const int32_t schem_max_y = origin.y + schematic.height - 1;
  const int32_t schem_min_z = origin.z;
  const int32_t schem_max_z = origin.z + schematic.length - 1;

  // 包围盒无相交检测
  if (chunk_max_x < schem_min_x || chunk_min_x > schem_max_x || chunk_max_z < schem_min_z ||
      chunk_min_z > schem_max_z) {
    return;
  }

  // 严格在交集边界内遍历（零浪费遍历）
  const int32_t start_x = std::max(chunk_min_x, schem_min_x);
  const int32_t end_x = std::min(chunk_max_x, schem_max_x);
  const int32_t start_y = std::clamp(schem_min_y, 0, 255);
  const int32_t end_y = std::clamp(schem_max_y, 0, 255);
  const int32_t start_z = std::max(chunk_min_z, schem_min_z);
  const int32_t end_z = std::min(chunk_max_z, schem_max_z);

  for (int32_t wy = start_y; wy <= end_y; ++wy) {
    const int32_t sy = wy - origin.y;
    for (int32_t wz = start_z; wz <= end_z; ++wz) {
      const int32_t sz = wz - origin.z;
      for (int32_t wx = start_x; wx <= end_x; ++wx) {
        const int32_t sx = wx - origin.x;
        const uint16_t state = schematic.getBlockState(sx, sy, sz);
        if (state != 0) {  // 跳过空气，保持虚空或已有方块
          chunk.setBlock(BlockPos{wx, wy, wz}, state);
        }
      }
    }
  }
}

void
SchematicLoader::pasteToWorld(
    const Schematic& schematic,
    const BlockPos& origin,
    World& world
) {
  if (!schematic.isValid()) {
    spdlog::warn("[SchematicLoader] Attempted to paste invalid schematic into world");
    return;
  }

  const int32_t min_x = origin.x;
  const int32_t max_x = origin.x + schematic.width - 1;
  const int32_t min_z = origin.z;
  const int32_t max_z = origin.z + schematic.length - 1;

  const int32_t min_cx = min_x >> 4;
  const int32_t max_cx = max_x >> 4;
  const int32_t min_cz = min_z >> 4;
  const int32_t max_cz = max_z >> 4;

  spdlog::info(
      "[SchematicLoader] Pasting schematic ({}x{}x{}) from Chunk ({}, {}) to ({}, {})",
      schematic.width,
      schematic.height,
      schematic.length,
      min_cx,
      min_cz,
      max_cx,
      max_cz
  );

  for (int32_t cz = min_cz; cz <= max_cz; ++cz) {
    for (int32_t cx = min_cx; cx <= max_cx; ++cx) {
      Chunk& chunk = world.getOrCreateChunk(ChunkPos{cx, cz});
      pasteToChunk(schematic, origin, chunk);
    }
  }
}

bool
SchematicLoader::loadIntoWorld(
    const std::filesystem::path& file_path,
    const BlockPos& origin,
    World& world
) {
  auto schem_opt = loadFromFile(file_path);
  if (!schem_opt) {
    spdlog::error("[SchematicLoader] Failed to load schematic from {}", file_path.string());
    return false;
  }

  pasteToWorld(*schem_opt, origin, world);
  return true;
}

}  // namespace mcpe::world
