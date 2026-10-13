/**
 * @file SchematicLoader.cc
 * @brief WorldEdit .schematic 经典建筑蓝图加载与解析器实现
 */

#include "world/SchematicLoader.h"

#include <spdlog/spdlog.h>
#include <zlib.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <string_view>

namespace mcpe::world {

namespace {

enum class NbtTag : uint8_t {
  End = 0,
  Byte = 1,
  Short = 2,
  Int = 3,
  Long = 4,
  Float = 5,
  Double = 6,
  ByteArray = 7,
  String = 8,
  List = 9,
  Compound = 10,
  IntArray = 11,
  LongArray = 12
};

class NbtReader {
 public:
  explicit NbtReader(std::span<const uint8_t> data) : data_(data) {}

  [[nodiscard]] bool
  hasRemaining(size_t bytes) const noexcept {
    return offset_ + bytes <= data_.size();
  }

  [[nodiscard]] size_t
  remaining() const noexcept {
    return data_.size() - offset_;
  }

  std::optional<uint8_t>
  readU8() noexcept {
    if (!hasRemaining(1)) return std::nullopt;
    return data_[offset_++];
  }

  std::optional<int16_t>
  readI16() noexcept {
    if (!hasRemaining(2)) return std::nullopt;
    uint16_t val = (static_cast<uint16_t>(data_[offset_]) << 8) |
                   static_cast<uint16_t>(data_[offset_ + 1]);
    offset_ += 2;
    return static_cast<int16_t>(val);
  }

  std::optional<int32_t>
  readI32() noexcept {
    if (!hasRemaining(4)) return std::nullopt;
    uint32_t val = (static_cast<uint32_t>(data_[offset_]) << 24) |
                   (static_cast<uint32_t>(data_[offset_ + 1]) << 16) |
                   (static_cast<uint32_t>(data_[offset_ + 2]) << 8) |
                   static_cast<uint32_t>(data_[offset_ + 3]);
    offset_ += 4;
    return static_cast<int32_t>(val);
  }

  std::optional<std::string_view>
  readString() noexcept {
    auto len_opt = readI16();
    if (!len_opt || *len_opt < 0) return std::nullopt;
    const size_t len = static_cast<size_t>(*len_opt);
    if (!hasRemaining(len)) return std::nullopt;
    std::string_view sv(reinterpret_cast<const char*>(data_.data() + offset_), len);
    offset_ += len;
    return sv;
  }

  std::optional<std::span<const uint8_t>>
  readByteArray() noexcept {
    auto len_opt = readI32();
    if (!len_opt || *len_opt < 0) return std::nullopt;
    const size_t len = static_cast<size_t>(*len_opt);
    if (!hasRemaining(len)) return std::nullopt;
    std::span<const uint8_t> span(data_.data() + offset_, len);
    offset_ += len;
    return span;
  }

  bool
  skip(size_t bytes) noexcept {
    if (!hasRemaining(bytes)) return false;
    offset_ += bytes;
    return true;
  }

  bool
  skipTag(NbtTag type) noexcept {
    switch (type) {
      case NbtTag::End:
        return true;
      case NbtTag::Byte:
        return skip(1);
      case NbtTag::Short:
        return skip(2);
      case NbtTag::Int:
      case NbtTag::Float:
        return skip(4);
      case NbtTag::Long:
      case NbtTag::Double:
        return skip(8);
      case NbtTag::ByteArray: {
        auto len = readI32();
        return len && *len >= 0 && skip(static_cast<size_t>(*len));
      }
      case NbtTag::String: {
        auto len = readI16();
        return len && *len >= 0 && skip(static_cast<size_t>(*len));
      }
      case NbtTag::List: {
        auto elem_type_opt = readU8();
        auto len_opt = readI32();
        if (!elem_type_opt || !len_opt || *len_opt < 0) return false;
        const auto elem_type = static_cast<NbtTag>(*elem_type_opt);
        const int32_t count = *len_opt;
        for (int32_t i = 0; i < count; ++i) {
          if (!skipTag(elem_type)) return false;
        }
        return true;
      }
      case NbtTag::Compound: {
        while (true) {
          auto tag_type_opt = readU8();
          if (!tag_type_opt) return false;
          auto tag_type = static_cast<NbtTag>(*tag_type_opt);
          if (tag_type == NbtTag::End) break;
          auto name_opt = readString();
          if (!name_opt || !skipTag(tag_type)) return false;
        }
        return true;
      }
      case NbtTag::IntArray: {
        auto len = readI32();
        return len && *len >= 0 && skip(static_cast<size_t>(*len) * 4);
      }
      case NbtTag::LongArray: {
        auto len = readI32();
        return len && *len >= 0 && skip(static_cast<size_t>(*len) * 8);
      }
      default:
        return false;
    }
  }

 private:
  std::span<const uint8_t> data_;
  size_t offset_{0};
};

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

}  // namespace mcpe::world
