/**
 * @file World.cc
 * @brief 世界容器与区块内存常驻管理器实现
 */

#include "world/World.h"

#include <spdlog/spdlog.h>

#include "world/AnvilLoader.h"

namespace mcpe::world {

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

void
World::loadSchematic(const Schematic& schematic, const BlockPos& origin) {
  if (!schematic.isValid()) {
    spdlog::warn("[World] Attempted to load invalid schematic into world");
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
      "[World] Pasting schematic ({}x{}x{}) from Chunk ({}, {}) to ({}, {})",
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
      Chunk& chunk = getOrCreateChunk(ChunkPos{cx, cz});
      SchematicLoader::pasteToChunk(schematic, origin, chunk);
    }
  }
}

bool
World::loadLobbySchematic(const std::filesystem::path& path, const BlockPos& origin) {
  auto schem_opt = SchematicLoader::loadFromFile(path);
  if (!schem_opt) {
    spdlog::error("[World] Failed to load lobby schematic from {}", path.string());
    return false;
  }

  loadSchematic(*schem_opt, origin);
  return true;
}

size_t
World::loadWorldFromZip(const std::filesystem::path& path) {
  return AnvilLoader::loadFromZip(path, *this);
}

size_t
World::loadWorldFromDirectory(const std::filesystem::path& path) {
  return AnvilLoader::loadFromDirectory(path, *this);
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

  const int32_t target_x = static_cast<int32_t>(std::floor(spawn_point_.x));
  const int32_t target_z = static_cast<int32_t>(std::floor(spawn_point_.z));

  int32_t ground_y = getHighestBlockY(target_x, target_z);
  if (ground_y >= 0) {
    spawn_point_.y = static_cast<double>(ground_y + 1);
    spdlog::info(
        "[World] Adjusted spawn point to safe ground at ({:.1f}, {:.1f}, {:.1f})",
        spawn_point_.x,
        spawn_point_.y,
        spawn_point_.z
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
          spawn_point_ = Vector3d{wx + 0.5, static_cast<double>(gy + 1), wz + 0.5};
          spdlog::info(
              "[World] Spawn was in void! Repositioned to nearest solid block at ({:.1f}, {:.1f}, {:.1f})",
              spawn_point_.x,
              spawn_point_.y,
              spawn_point_.z
          );
          return;
        }
      }
    }
  }
}

}  // namespace mcpe::world
