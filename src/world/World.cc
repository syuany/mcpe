/**
 * @file World.cc
 * @brief 世界容器与区块内存常驻管理器实现
 */

#include "world/World.h"

#include <spdlog/spdlog.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>
#include <string>
#include <string_view>

#include "common/IniConfig.h"

namespace mcpe::world {

namespace {
constexpr char kBakedMapMagic[4] = {'M', 'C', 'P', 'E'};
constexpr uint32_t kBakedMapVersion = 1;
}  // namespace

const Chunk*
World::getChunk(ChunkPos pos) const {
  auto it = chunks_.find(pos);
  if (it != chunks_.end()) {
    return it->second.get();
  }
  return nullptr;
}

Chunk*
World::getChunk(ChunkPos pos) {
  auto it = chunks_.find(pos);
  if (it != chunks_.end()) {
    return it->second.get();
  }
  return nullptr;
}

Chunk&
World::getOrCreateChunk(ChunkPos pos) {
  auto& chunk_ptr = chunks_[pos];
  if (!chunk_ptr) {
    chunk_ptr = std::make_unique<Chunk>(pos);
  }
  return *chunk_ptr;
}

bool
World::saveBakedMap(const std::filesystem::path& path) const {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    spdlog::error("[World] Failed to open '{}' for writing baked map", path.string());
    return false;
  }

  const Vector3d sp = spawnPoint();
  const Rotation rot = spawnRotation();
  const uint32_t chunk_count = static_cast<uint32_t>(chunks_.size());

  // 1. 写入文件头: Magic(4B) + Version(4B) + SpawnPoint(24B) + Rotation(8B) + ChunkCount(4B)
  out.write(kBakedMapMagic, 4);
  out.write(reinterpret_cast<const char*>(&kBakedMapVersion), sizeof(kBakedMapVersion));
  out.write(reinterpret_cast<const char*>(&sp.x), sizeof(double));
  out.write(reinterpret_cast<const char*>(&sp.y), sizeof(double));
  out.write(reinterpret_cast<const char*>(&sp.z), sizeof(double));
  out.write(reinterpret_cast<const char*>(&rot.yaw), sizeof(float));
  out.write(reinterpret_cast<const char*>(&rot.pitch), sizeof(float));
  out.write(reinterpret_cast<const char*>(&chunk_count), sizeof(chunk_count));

  // 2. 写入每个 Chunk 的坐标、Section 掩码、Biomes 及非空 Section 连续方块数组
  for (const auto& [cpos, chunk_ptr] : chunks_) {
    if (!chunk_ptr) continue;
    const int32_t cx = cpos.x;
    const int32_t cz = cpos.z;
    const uint16_t bitmask = chunk_ptr->primaryBitMask();

    out.write(reinterpret_cast<const char*>(&cx), sizeof(cx));
    out.write(reinterpret_cast<const char*>(&cz), sizeof(cz));
    out.write(reinterpret_cast<const char*>(&bitmask), sizeof(bitmask));
    out.write(
        reinterpret_cast<const char*>(chunk_ptr->biomes().data()),
        Chunk::kBiomeCount
    );

    for (size_t sec_idx = 0; sec_idx < Chunk::kSectionCount; ++sec_idx) {
      if ((bitmask & (1U << sec_idx)) != 0) {
        const ChunkSection* sec = chunk_ptr->getSection(sec_idx);
        if (sec) {
          out.write(
              reinterpret_cast<const char*>(sec->blocks().data()),
              ChunkSection::kBlockCount * sizeof(uint16_t)
          );
        }
      }
    }
  }

  return out.good();
}

size_t
World::loadBakedMap(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    spdlog::warn("[World] Baked map file not found: {}", path.string());
    return 0;
  }

  char magic[4]{};
  uint32_t version = 0;
  Vector3d sp{};
  Rotation rot{};
  uint32_t chunk_count = 0;

  if (!in.read(magic, 4) || std::memcmp(magic, kBakedMapMagic, 4) != 0) {
    spdlog::error("[World] Invalid magic header in baked map: {}", path.string());
    return 0;
  }

  if (!in.read(reinterpret_cast<char*>(&version), sizeof(version)) ||
      version != kBakedMapVersion) {
    spdlog::error("[World] Unsupported baked map version {} in {}", version, path.string());
    return 0;
  }

  if (!in.read(reinterpret_cast<char*>(&sp.x), sizeof(double)) ||
      !in.read(reinterpret_cast<char*>(&sp.y), sizeof(double)) ||
      !in.read(reinterpret_cast<char*>(&sp.z), sizeof(double)) ||
      !in.read(reinterpret_cast<char*>(&rot.yaw), sizeof(float)) ||
      !in.read(reinterpret_cast<char*>(&rot.pitch), sizeof(float)) ||
      !in.read(reinterpret_cast<char*>(&chunk_count), sizeof(chunk_count))) {
    spdlog::error("[World] Truncated header in baked map: {}", path.string());
    return 0;
  }

  setSpawnPoint(sp);
  setSpawnRotation(rot);
  chunks_.clear();
  chunks_.reserve(chunk_count);

  std::array<uint8_t, Chunk::kBiomeCount> biomes_buf{};
  std::array<uint16_t, ChunkSection::kBlockCount> blocks_buf{};

  size_t loaded_chunks = 0;
  for (uint32_t i = 0; i < chunk_count; ++i) {
    int32_t cx = 0;
    int32_t cz = 0;
    uint16_t bitmask = 0;

    if (!in.read(reinterpret_cast<char*>(&cx), sizeof(cx)) ||
        !in.read(reinterpret_cast<char*>(&cz), sizeof(cz)) ||
        !in.read(reinterpret_cast<char*>(&bitmask), sizeof(bitmask)) ||
        !in.read(reinterpret_cast<char*>(biomes_buf.data()), Chunk::kBiomeCount)) {
      spdlog::error("[World] Corrupted chunk header at index {} in {}", i, path.string());
      break;
    }

    Chunk& chunk = getOrCreateChunk(ChunkPos{cx, cz});
    chunk.setBiomes(biomes_buf);

    for (size_t sec_idx = 0; sec_idx < Chunk::kSectionCount; ++sec_idx) {
      if ((bitmask & (1U << sec_idx)) != 0) {
        if (!in.read(
                reinterpret_cast<char*>(blocks_buf.data()),
                ChunkSection::kBlockCount * sizeof(uint16_t)
            )) {
          spdlog::error("[World] Corrupted section data in chunk ({}, {})", cx, cz);
          return loaded_chunks;
        }
        chunk.getOrCreateSection(sec_idx).loadRawBlocks(blocks_buf);
      }
    }
    ++loaded_chunks;
  }

  spdlog::info(
      "[World] Loaded {} baked chunks from '{}', spawn=({:.1f}, {:.1f}, {:.1f})",
      loaded_chunks,
      path.string(),
      sp.x,
      sp.y,
      sp.z
  );
  return loaded_chunks;
}

bool
World::loadSpawnConfig(const std::filesystem::path& path) {
  if (path.extension() == ".ini") {
    common::IniConfig ini;
    if (!ini.loadFromFile(path)) {
      return false;
    }
    if (!ini.hasKey("World", "spawn_x") || !ini.hasKey("World", "spawn_y") ||
        !ini.hasKey("World", "spawn_z")) {
      return false;
    }
    Vector3d pos = spawnPoint();
    Rotation rot = spawnRotation();
    pos.x = ini.getDouble("World", "spawn_x", pos.x);
    pos.y = ini.getDouble("World", "spawn_y", pos.y);
    pos.z = ini.getDouble("World", "spawn_z", pos.z);
    rot.yaw = static_cast<float>(ini.getDouble("World", "spawn_yaw", rot.yaw));
    rot.pitch = static_cast<float>(ini.getDouble("World", "spawn_pitch", rot.pitch));
    setSpawnPoint(pos);
    setSpawnRotation(rot);
    if (ini.hasKey("World", "fall_reset_y")) {
      setFallResetY(ini.getDouble("World", "fall_reset_y", fallResetY()));
    }
    return true;
  }

  std::ifstream file(path);
  if (!file.is_open()) {
    return false;
  }

  Vector3d pos = spawnPoint();
  Rotation rot = spawnRotation();
  std::string line;
  bool has_x = false;
  bool has_y = false;
  bool has_z = false;

  while (std::getline(file, line)) {
    auto parse_field = [&](std::string_view key, auto& out_val) {
      auto pos_key = line.find(key);
      if (pos_key != std::string::npos) {
        auto colon = line.find(':', pos_key + key.size());
        if (colon != std::string::npos) {
          char* end = nullptr;
          const char* str_start = line.c_str() + colon + 1;
          double v = std::strtod(str_start, &end);
          if (end != str_start) {
            out_val = static_cast<std::decay_t<decltype(out_val)>>(v);
            return true;
          }
        }
      }
      return false;
    };
    if (parse_field("\"x\"", pos.x)) has_x = true;
    if (parse_field("\"y\"", pos.y)) has_y = true;
    if (parse_field("\"z\"", pos.z)) has_z = true;
    parse_field("\"yaw\"", rot.yaw);
    parse_field("\"pitch\"", rot.pitch);
  }

  if (has_x && has_y && has_z) {
    setSpawnPoint(pos);
    setSpawnRotation(rot);
    return true;
  }
  return false;
}

bool
World::saveSpawnConfig(const std::filesystem::path& path) const {
  if (path.extension() == ".ini") {
    common::IniConfig ini;
    (void)ini.loadFromFile(path);  // 保留文件中已有的其他 Section（如 [Parkour]）
    const Vector3d pos = spawnPoint();
    const Rotation rot = spawnRotation();
    ini.setDouble("World", "spawn_x", pos.x);
    ini.setDouble("World", "spawn_y", pos.y);
    ini.setDouble("World", "spawn_z", pos.z);
    ini.setDouble("World", "spawn_yaw", rot.yaw);
    ini.setDouble("World", "spawn_pitch", rot.pitch);
    ini.setDouble("World", "fall_reset_y", fallResetY());
    return ini.saveToFile(path);
  }

  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }
  const Vector3d pos = spawnPoint();
  const Rotation rot = spawnRotation();
  file << std::format(
      "{{\n"
      "  \"x\": {:.2f},\n"
      "  \"y\": {:.2f},\n"
      "  \"z\": {:.2f},\n"
      "  \"yaw\": {:.2f},\n"
      "  \"pitch\": {:.2f}\n"
      "}}\n",
      pos.x,
      pos.y,
      pos.z,
      rot.yaw,
      rot.pitch
  );
  return true;
}

std::vector<ChunkPos>
World::getAllChunkPositions() const {
  std::vector<ChunkPos> positions;
  positions.reserve(chunks_.size());
  for (const auto& [pos, _] : chunks_) {
    positions.push_back(pos);
  }
  return positions;
}

int32_t
World::getHighestBlockY(int32_t x, int32_t z) const {
  const ChunkPos cpos{x >> 4, z >> 4};
  const Chunk* chunk = getChunk(cpos);
  if (!chunk) {
    return -1;
  }

  for (int32_t y = 255; y >= 0; --y) {
    if (chunk->getBlock(BlockPos{x, y, z}) != BlockStates::kAir) {
      return y;
    }
  }
  return -1;
}

void
World::adjustSpawnPointToSafeGround() {
  if (chunks_.empty()) {
    return;
  }

  Vector3d current_spawn = spawnPoint();
  const int32_t target_x = static_cast<int32_t>(std::floor(current_spawn.x));
  const int32_t target_z = static_cast<int32_t>(std::floor(current_spawn.z));

  int32_t ground_y = getHighestBlockY(target_x, target_z);
  if (ground_y >= 0) {
    current_spawn.y = static_cast<double>(ground_y + 1);
    setSpawnPoint(current_spawn);
    spdlog::info(
        "[World] Adjusted spawn point to safe ground at ({:.1f}, {:.1f}, {:.1f})",
        current_spawn.x,
        current_spawn.y,
        current_spawn.z
    );
    return;
  }

  // 若当前出生点下方完全是空气（虚空），在已加载的区块中寻找最近的非空方块
  for (const auto& [cpos, chunk] : chunks_) {
    for (int32_t lz = 8; lz < 16; ++lz) {
      for (int32_t lx = 8; lx < 16; ++lx) {
        const int32_t wx = (cpos.x << 4) + lx;
        const int32_t wz = (cpos.z << 4) + lz;
        int32_t gy = getHighestBlockY(wx, wz);
        if (gy >= 0) {
          const Vector3d safe_pos{wx + 0.5, static_cast<double>(gy + 1), wz + 0.5};
          setSpawnPoint(safe_pos);
          spdlog::info(
              "[World] Spawn was in void! Repositioned to nearest solid block at ({:.1f}, {:.1f}, {:.1f})",
              safe_pos.x,
              safe_pos.y,
              safe_pos.z
          );
          return;
        }
      }
    }
  }
}

}  // namespace mcpe::world
