/**
 * @file AnvilLoader.cc
 * @brief Minecraft 原版 Anvil (.mca) 与世界存档 (.zip / 文件夹) 加载器实现
 */

#include "world/AnvilLoader.h"

#include <spdlog/spdlog.h>
#include <zlib.h>

#include <cstring>
#include <fstream>
#include <regex>

#include "common/NbtReader.h"
#include "common/ZipArchive.h"

namespace mcpe::world {

namespace {

inline uint32_t
readU32BE(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

std::optional<std::vector<uint8_t>>
decompressZlibBuffer(std::span<const uint8_t> compressed, int window_bits) {
  if (compressed.empty()) return std::nullopt;

  z_stream strm{};
  strm.next_in = const_cast<Bytef*>(compressed.data());
  strm.avail_in = compressed.size();

  if (inflateInit2(&strm, window_bits) != Z_OK) {
    return std::nullopt;
  }

  std::vector<uint8_t> out;
  out.resize(std::max<size_t>(compressed.size() * 4, 32768));

  strm.next_out = reinterpret_cast<Bytef*>(out.data());
  strm.avail_out = out.size();

  while (true) {
    int ret = inflate(&strm, Z_NO_FLUSH);
    if (ret == Z_STREAM_END) {
      out.resize(strm.total_out);
      inflateEnd(&strm);
      return out;
    }
    if (ret != Z_OK) {
      inflateEnd(&strm);
      return std::nullopt;
    }
    // 扩容缓冲区
    const size_t old_size = out.size();
    out.resize(old_size * 2);
    strm.next_out = reinterpret_cast<Bytef*>(out.data() + old_size);
    strm.avail_out = old_size;
  }
}

}  // namespace

std::optional<WorldSaveMetadata>
AnvilLoader::parseLevelDat(std::span<const uint8_t> level_dat_bytes) {
  // level.dat 通常使用标准 Gzip 压缩 (windowBits = 15 + 32)
  auto decompressed = decompressZlibBuffer(level_dat_bytes, 15 + 32);
  if (!decompressed) {
    // 降级尝试标准 Zlib 头 (windowBits = 15)
    decompressed = decompressZlibBuffer(level_dat_bytes, 15);
  }
  if (!decompressed) {
    spdlog::warn("[AnvilLoader] Failed to decompress level.dat");
    return std::nullopt;
  }

  common::NbtReader reader(*decompressed);
  auto root_type = reader.readU8();
  if (!root_type || static_cast<common::NbtTag>(*root_type) != common::NbtTag::Compound) {
    return std::nullopt;
  }
  reader.readString();  // root name 通常为空或 ""

  WorldSaveMetadata meta;
  bool found_data_compound = false;

  // 寻找根 Compound 下的 "Data" Compound
  while (reader.hasRemaining()) {
    auto tag_type_opt = reader.readU8();
    if (!tag_type_opt) break;
    auto tag_type = static_cast<common::NbtTag>(*tag_type_opt);
    if (tag_type == common::NbtTag::End) break;
    auto name_opt = reader.readString();
    if (!name_opt) break;

    if (tag_type == common::NbtTag::Compound && *name_opt == "Data") {
      found_data_compound = true;
      // 解析 Data Compound
      int32_t sx = 0, sy = 64, sz = 0;
      bool has_sx = false, has_sy = false, has_sz = false;

      while (reader.hasRemaining()) {
        auto sub_type_opt = reader.readU8();
        if (!sub_type_opt) break;
        auto sub_type = static_cast<common::NbtTag>(*sub_type_opt);
        if (sub_type == common::NbtTag::End) break;
        auto sub_name_opt = reader.readString();
        if (!sub_name_opt) break;

        if (sub_type == common::NbtTag::Int && *sub_name_opt == "SpawnX") {
          auto val = reader.readI32();
          if (val) { sx = *val; has_sx = true; }
        } else if (sub_type == common::NbtTag::Int && *sub_name_opt == "SpawnY") {
          auto val = reader.readI32();
          if (val) { sy = *val; has_sy = true; }
        } else if (sub_type == common::NbtTag::Int && *sub_name_opt == "SpawnZ") {
          auto val = reader.readI32();
          if (val) { sz = *val; has_sz = true; }
        } else if (sub_type == common::NbtTag::String && *sub_name_opt == "LevelName") {
          auto val = reader.readString();
          if (val) meta.level_name = std::string(*val);
        } else if (sub_type == common::NbtTag::Compound && *sub_name_opt == "Player") {
          while (reader.hasRemaining()) {
            auto p_type_opt = reader.readU8();
            if (!p_type_opt) break;
            auto p_type = static_cast<common::NbtTag>(*p_type_opt);
            if (p_type == common::NbtTag::End) break;
            auto p_name_opt = reader.readString();
            if (!p_name_opt) break;

            if (p_type == common::NbtTag::List && *p_name_opt == "Pos") {
              auto elem_type_opt = reader.readU8();
              auto len_opt = reader.readI32();
              if (elem_type_opt && len_opt && *len_opt == 3 &&
                  static_cast<common::NbtTag>(*elem_type_opt) == common::NbtTag::Double) {
                auto px = reader.readDouble();
                auto py = reader.readDouble();
                auto pz = reader.readDouble();
                if (px && py && pz) {
                  meta.player_pos = Vector3d{*px, *py, *pz};
                }
              } else if (elem_type_opt && len_opt && *len_opt > 0) {
                for (int32_t i = 0; i < *len_opt; ++i) {
                  reader.skipTag(static_cast<common::NbtTag>(*elem_type_opt));
                }
              }
            } else {
              if (!reader.skipTag(p_type)) break;
            }
          }
        } else {
          if (!reader.skipTag(sub_type)) break;
        }
      }

      // 优先采用地图作者保存时的 Player 真实坐标，无则回退至 SpawnX/Y/Z
      if (meta.player_pos.has_value()) {
        meta.spawn_point = *meta.player_pos;
      } else if (has_sx && has_sz) {
        (void)has_sy;
        meta.spawn_point = Vector3d{sx + 0.5, static_cast<double>(sy), sz + 0.5};
      }
      break;
    } else {
      if (!reader.skipTag(tag_type)) break;
    }
  }

  if (!found_data_compound) {
    return std::nullopt;
  }

  return meta;
}

size_t
AnvilLoader::parseRegionFile(std::span<const uint8_t> mca_bytes, World& world) {
  if (mca_bytes.size() < 8192) {
    return 0;
  }

  size_t loaded_chunks = 0;

  // 遍历 32x32 = 1024 个 Chunk 槽位
  for (int cz = 0; cz < 32; ++cz) {
    for (int cx = 0; cx < 32; ++cx) {
      const size_t offset_idx = 4 * (cx + cz * 32);
      const uint32_t entry_val = readU32BE(mca_bytes.data() + offset_idx);
      const uint32_t sector_offset = entry_val >> 8;
      const uint32_t sector_count = entry_val & 0xFF;

      if (sector_offset == 0 || sector_count == 0) {
        continue;
      }

      const size_t file_offset = sector_offset * 4096;
      if (file_offset + 5 > mca_bytes.size()) {
        continue;
      }

      const uint32_t data_len = readU32BE(mca_bytes.data() + file_offset);
      const uint8_t comp_type = mca_bytes[file_offset + 4];

      if (data_len <= 1 || file_offset + 4 + data_len > mca_bytes.size()) {
        continue;
      }

      std::span<const uint8_t> compressed_payload(
          mca_bytes.data() + file_offset + 5,
          data_len - 1
      );

      // 解压 Chunk NBT (comp_type == 2 为 zlib 标准压缩)
      int window_bits = (comp_type == 2) ? 15 : (15 + 32);
      auto chunk_nbt = decompressZlibBuffer(compressed_payload, window_bits);
      if (!chunk_nbt) {
        continue;
      }

      // 解析 Chunk NBT
      common::NbtReader r(*chunk_nbt);
      auto root_type_opt = r.readU8();
      if (!root_type_opt || static_cast<common::NbtTag>(*root_type_opt) != common::NbtTag::Compound) {
        continue;
      }
      r.readString();  // root name

      int32_t chunk_x = 0;
      int32_t chunk_z = 0;
      bool has_coords = false;

      struct PendingSection {
        int8_t y{-1};
        std::span<const uint8_t> blocks;
        std::span<const uint8_t> data;
        std::span<const uint8_t> add;
      };
      std::vector<PendingSection> pending_sections;
      pending_sections.reserve(16);

      // 寻找 Level Compound
      while (r.hasRemaining()) {
        auto tag_type_opt = r.readU8();
        if (!tag_type_opt) break;
        auto tag_type = static_cast<common::NbtTag>(*tag_type_opt);
        if (tag_type == common::NbtTag::End) break;
        auto tag_name_opt = r.readString();
        if (!tag_name_opt) break;

        if (tag_type == common::NbtTag::Compound && *tag_name_opt == "Level") {
          // 在 Level Compound 中寻找 xPos, zPos, Sections
          while (r.hasRemaining()) {
            auto l_type_opt = r.readU8();
            if (!l_type_opt) break;
            auto l_type = static_cast<common::NbtTag>(*l_type_opt);
            if (l_type == common::NbtTag::End) break;
            auto l_name_opt = r.readString();
            if (!l_name_opt) break;

            if (l_type == common::NbtTag::Int && *l_name_opt == "xPos") {
              auto val = r.readI32();
              if (val) { chunk_x = *val; }
            } else if (l_type == common::NbtTag::Int && *l_name_opt == "zPos") {
              auto val = r.readI32();
              if (val) { chunk_z = *val; has_coords = true; }
            } else if (l_type == common::NbtTag::List && *l_name_opt == "Sections") {
              auto sec_elem_opt = r.readU8();
              auto sec_len_opt = r.readI32();
              if (!sec_elem_opt || !sec_len_opt || *sec_len_opt < 0) break;

              const int32_t sec_count = *sec_len_opt;
              if (static_cast<common::NbtTag>(*sec_elem_opt) == common::NbtTag::Compound) {
                for (int32_t s = 0; s < sec_count; ++s) {
                  PendingSection sec;

                  while (r.hasRemaining()) {
                    auto s_type_opt = r.readU8();
                    if (!s_type_opt) break;
                    auto s_type = static_cast<common::NbtTag>(*s_type_opt);
                    if (s_type == common::NbtTag::End) break;
                    auto s_name_opt = r.readString();
                    if (!s_name_opt) break;

                    if (s_type == common::NbtTag::Byte && *s_name_opt == "Y") {
                      auto val = r.readI8();
                      if (val) sec.y = *val;
                    } else if (s_type == common::NbtTag::ByteArray && *s_name_opt == "Blocks") {
                      auto arr = r.readByteArray();
                      if (arr && arr->size() == 4096) sec.blocks = *arr;
                    } else if (s_type == common::NbtTag::ByteArray && *s_name_opt == "Data") {
                      auto arr = r.readByteArray();
                      if (arr && arr->size() == 2048) sec.data = *arr;
                    } else if (s_type == common::NbtTag::ByteArray && *s_name_opt == "Add") {
                      auto arr = r.readByteArray();
                      if (arr && arr->size() == 2048) sec.add = *arr;
                    } else {
                      if (!r.skipTag(s_type)) break;
                    }
                  }

                  if (sec.y >= 0 && sec.y < 16 && sec.blocks.size() == 4096 && sec.data.size() == 2048) {
                    pending_sections.push_back(sec);
                  }
                }
              } else {
                for (int32_t s = 0; s < sec_count; ++s) {
                  r.skipTag(static_cast<common::NbtTag>(*sec_elem_opt));
                }
              }
            } else {
              if (!r.skipTag(l_type)) break;
            }
          }
          break;
        } else {
          if (!r.skipTag(tag_type)) break;
        }
      }

      // 所有标签解析完毕，填充区块
      if (has_coords && !pending_sections.empty()) {
        auto& chunk = world.getOrCreateChunk(ChunkPos{chunk_x, chunk_z});
        for (const auto& sec : pending_sections) {
          const bool has_add = (sec.add.size() == 2048);
          for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
              for (int x = 0; x < 16; ++x) {
                const size_t b_idx = (y * 16 + z) * 16 + x;
                const uint8_t bid = sec.blocks[b_idx];
                const uint8_t meta = (b_idx % 2 == 0)
                                         ? (sec.data[b_idx / 2] & 0x0F)
                                         : ((sec.data[b_idx / 2] >> 4) & 0x0F);
                uint16_t block_id = bid;
                if (has_add) {
                  const uint8_t add_nibble = (b_idx % 2 == 0)
                                                 ? (sec.add[b_idx / 2] & 0x0F)
                                                 : ((sec.add[b_idx / 2] >> 4) & 0x0F);
                  block_id |= (static_cast<uint16_t>(add_nibble) << 8);
                }
                const uint16_t state_id = (block_id << 4) | meta;
                if (state_id != 0) {
                  chunk.setBlock(
                      BlockPos{chunk_x * 16 + x, sec.y * 16 + y, chunk_z * 16 + z},
                      state_id
                  );
                }
              }
            }
          }
        }
        ++loaded_chunks;
      }
    }
  }

  return loaded_chunks;
}

size_t
AnvilLoader::loadFromZip(const std::filesystem::path& zip_path, World& world) {
  auto archive = common::ZipArchive::openFile(zip_path);
  if (!archive) {
    spdlog::warn("[AnvilLoader] Failed to open ZIP archive: {}", zip_path.string());
    return 0;
  }

  // 1. 尝试寻找并加载 level.dat
  for (const auto& entry : archive->entries()) {
    if (std::string_view(entry.name).ends_with("level.dat")) {
      auto level_bytes = archive->extractEntry(entry.name);
      if (level_bytes) {
        auto meta = parseLevelDat(*level_bytes);
        if (meta) {
          world.setSpawnPoint(meta->spawn_point);
          spdlog::info(
              "[AnvilLoader] Loaded level.dat from '{}': Name='{}', Spawn=({:.1f}, {:.1f}, {:.1f})",
              entry.name,
              meta->level_name,
              meta->spawn_point.x,
              meta->spawn_point.y,
              meta->spawn_point.z
          );
          break;
        }
      }
    }
  }

  // 2. 遍历并流式加载所有主世界 region/*.mca 文件
  size_t total_chunks = 0;
  for (const auto& entry : archive->entries()) {
    std::string_view sv(entry.name);
    if (sv.ends_with(".mca") &&
        sv.find("DIM-1") == std::string_view::npos &&
        sv.find("DIM1") == std::string_view::npos &&
        sv.find("TEMP") == std::string_view::npos) {
      auto mca_bytes = archive->extractEntry(entry.name);
      if (mca_bytes && !mca_bytes->empty()) {
        size_t chunks = parseRegionFile(*mca_bytes, world);
        total_chunks += chunks;
        spdlog::debug(
            "[AnvilLoader] Parsed Region '{}' ({} Chunks loaded)",
            entry.name,
            chunks
        );
      }
    }
  }

  // 3. 自适应修正出生点至安全实体方块表面，杜绝虚空或凌空坠落
  world.adjustSpawnPointToSafeGround();

  spdlog::info(
      "[AnvilLoader] Successfully loaded {} Chunks from ZIP: {}",
      total_chunks,
      zip_path.filename().string()
  );

  return total_chunks;
}

size_t
AnvilLoader::loadFromDirectory(const std::filesystem::path& dir_path, World& world) {
  if (!std::filesystem::exists(dir_path) || !std::filesystem::is_directory(dir_path)) {
    spdlog::warn("[AnvilLoader] Directory not found: {}", dir_path.string());
    return 0;
  }

  // 1. 尝试加载 level.dat
  const auto level_file = dir_path / "level.dat";
  if (std::filesystem::exists(level_file)) {
    std::ifstream is(level_file, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
    auto meta = parseLevelDat(bytes);
    if (meta) {
      world.setSpawnPoint(meta->spawn_point);
      spdlog::info(
          "[AnvilLoader] Loaded level.dat: Name='{}', Spawn=({:.1f}, {:.1f}, {:.1f})",
          meta->level_name,
          meta->spawn_point.x,
          meta->spawn_point.y,
          meta->spawn_point.z
      );
    }
  }

  // 2. 遍历 region 目录
  const auto region_dir = dir_path / "region";
  if (!std::filesystem::exists(region_dir)) {
    return 0;
  }

  size_t total_chunks = 0;
  for (const auto& entry : std::filesystem::directory_iterator(region_dir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".mca") {
      std::ifstream is(entry.path(), std::ios::binary);
      std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(is)), std::istreambuf_iterator<char>());
      size_t chunks = parseRegionFile(bytes, world);
      total_chunks += chunks;
    }
  }

  // 3. 自适应修正出生点至安全实体方块表面
  world.adjustSpawnPointToSafeGround();

  spdlog::info(
      "[AnvilLoader] Successfully loaded {} Chunks from directory: {}",
      total_chunks,
      dir_path.string()
  );

  return total_chunks;
}

}  // namespace mcpe::world
