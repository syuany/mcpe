/**
 * @file ZipArchive.cc
 * @brief 轻量级 ZIP 归档读取器实现（Foundations 基础层）
 */

#include "ZipArchive.h"

#include <spdlog/spdlog.h>
#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>

namespace mcpe::common {

namespace {

constexpr uint32_t kEocdSignature = 0x06054b50;
constexpr uint32_t kCentralDirSignature = 0x02014b50;
constexpr uint32_t kLocalHeaderSignature = 0x04034b50;

inline uint16_t
readU16LE(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t
readU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

std::optional<ZipArchive>
ZipArchive::openFile(const std::filesystem::path& file_path) {
  std::ifstream file(file_path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    spdlog::warn("[ZipArchive] Cannot open file: {}", file_path.string());
    return std::nullopt;
  }

  const auto file_size = file.tellg();
  if (file_size < 22) {  // 最小 EOCD 长度为 22 字节
    spdlog::warn("[ZipArchive] File is too small for a valid zip: {}", file_path.string());
    return std::nullopt;
  }

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
  if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
    spdlog::warn("[ZipArchive] Failed to read file bytes: {}", file_path.string());
    return std::nullopt;
  }

  return openMemory(buffer);
}

std::optional<ZipArchive>
ZipArchive::openMemory(std::span<const uint8_t> buffer) {
  if (buffer.size() < 22) {
    return std::nullopt;
  }

  ZipArchive archive;
  archive.data_.assign(buffer.begin(), buffer.end());
  if (!archive.parseCentralDirectory()) {
    return std::nullopt;
  }

  return archive;
}

bool
ZipArchive::parseCentralDirectory() {
  const size_t total_size = data_.size();
  const size_t max_search = std::min<size_t>(total_size, 65535 + 22);
  const size_t search_start = total_size - max_search;

  size_t eocd_pos = total_size;
  for (size_t i = total_size - 22;; --i) {
    if (readU32LE(data_.data() + i) == kEocdSignature) {
      eocd_pos = i;
      break;
    }
    if (i == search_start) {
      break;
    }
  }

  if (eocd_pos == total_size) {
    spdlog::warn("[ZipArchive] EOCD signature not found in archive");
    return false;
  }

  const uint8_t* eocd = data_.data() + eocd_pos;
  const uint16_t total_entries = readU16LE(eocd + 10);
  const uint32_t cd_size = readU32LE(eocd + 12);
  const uint32_t cd_offset = readU32LE(eocd + 16);

  if (cd_offset + cd_size > total_size) {
    spdlog::warn("[ZipArchive] Central directory offset/size exceeds archive boundaries");
    return false;
  }

  entries_.clear();
  entries_.reserve(total_entries);

  size_t cur = cd_offset;
  for (uint16_t i = 0; i < total_entries && cur + 46 <= total_size; ++i) {
    const uint8_t* p = data_.data() + cur;
    if (readU32LE(p) != kCentralDirSignature) {
      spdlog::warn("[ZipArchive] Invalid Central Directory entry signature at offset {}", cur);
      return false;
    }

    ZipEntry entry;
    entry.compression_method = readU16LE(p + 10);
    entry.compressed_size = readU32LE(p + 20);
    entry.uncompressed_size = readU32LE(p + 24);
    const uint16_t name_len = readU16LE(p + 28);
    const uint16_t extra_len = readU16LE(p + 30);
    const uint16_t comment_len = readU16LE(p + 32);
    entry.local_header_offset = readU32LE(p + 42);

    if (cur + 46 + name_len > total_size) {
      return false;
    }

    entry.name = std::string(reinterpret_cast<const char*>(p + 46), name_len);
    entry.is_directory = (!entry.name.empty() && (entry.name.back() == '/' || entry.name.back() == '\\'));

    entries_.push_back(std::move(entry));
    cur += (46 + name_len + extra_len + comment_len);
  }

  return true;
}

bool
ZipArchive::hasEntry(std::string_view entry_name) const {
  return std::any_of(entries_.begin(), entries_.end(), [entry_name](const ZipEntry& e) {
    return e.name == entry_name;
  });
}

std::vector<std::string>
ZipArchive::entryNames() const {
  std::vector<std::string> names;
  names.reserve(entries_.size());
  for (const auto& e : entries_) {
    names.push_back(e.name);
  }
  return names;
}

std::optional<std::string>
ZipArchive::findFirstByExtension(std::string_view extension) const {
  for (const auto& e : entries_) {
    if (!e.is_directory && e.name.size() >= extension.size()) {
      if (std::string_view(e.name).ends_with(extension)) {
        return e.name;
      }
    }
  }
  return std::nullopt;
}

std::optional<std::vector<uint8_t>>
ZipArchive::extractEntry(std::string_view entry_name) const {
  const auto it = std::find_if(entries_.begin(), entries_.end(), [entry_name](const ZipEntry& e) {
    return e.name == entry_name;
  });

  if (it == entries_.end()) {
    spdlog::warn("[ZipArchive] Entry '{}' not found", entry_name);
    return std::nullopt;
  }

  const ZipEntry& entry = *it;
  if (entry.is_directory) {
    return std::vector<uint8_t>{};
  }

  if (entry.local_header_offset + 30 > data_.size()) {
    return std::nullopt;
  }

  const uint8_t* loc = data_.data() + entry.local_header_offset;
  if (readU32LE(loc) != kLocalHeaderSignature) {
    spdlog::warn("[ZipArchive] Corrupted local header signature for '{}'", entry_name);
    return std::nullopt;
  }

  const uint16_t loc_name_len = readU16LE(loc + 26);
  const uint16_t loc_extra_len = readU16LE(loc + 28);
  const size_t data_start = entry.local_header_offset + 30 + loc_name_len + loc_extra_len;

  if (data_start + entry.compressed_size > data_.size()) {
    return std::nullopt;
  }

  // 1. 无压缩模式 (Stored = 0)
  if (entry.compression_method == 0) {
    std::vector<uint8_t> result(entry.uncompressed_size);
    std::memcpy(result.data(), data_.data() + data_start, entry.uncompressed_size);
    return result;
  }

  // 2. 标准 Deflate 模式 (Deflated = 8)
  if (entry.compression_method == 8) {
    std::vector<uint8_t> result(entry.uncompressed_size);

    z_stream strm{};
    strm.next_in = const_cast<Bytef*>(data_.data() + data_start);
    strm.avail_in = entry.compressed_size;
    strm.next_out = reinterpret_cast<Bytef*>(result.data());
    strm.avail_out = entry.uncompressed_size;

    // 使用 raw deflate (windowBits = -15) 跳过 zlib header 与 Adler32
    if (inflateInit2(&strm, -MAX_WBITS) != Z_OK) {
      spdlog::warn("[ZipArchive] Failed to initialize raw inflate for '{}'", entry_name);
      return std::nullopt;
    }

    const int ret = inflate(&strm, Z_FINISH);
    inflateEnd(&strm);

    if (ret != Z_STREAM_END && ret != Z_OK) {
      spdlog::warn("[ZipArchive] Decompression failed for '{}' (zlib code {})", entry_name, ret);
      return std::nullopt;
    }

    return result;
  }

  spdlog::warn(
      "[ZipArchive] Unsupported compression method {} for '{}'",
      entry.compression_method,
      entry_name
  );
  return std::nullopt;
}

}  // namespace mcpe::common
