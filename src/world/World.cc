/**
 * @file World.cc
 * @brief 世界容器与区块内存常驻管理器实现
 */

#include "world/World.h"

#include <spdlog/spdlog.h>

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

std::vector<ChunkPos>
World::getAllChunkPositions() const {
  std::vector<ChunkPos> positions;
  positions.reserve(chunks_.size());
  for (const auto& [pos, _] : chunks_) {
    positions.push_back(pos);
  }
  return positions;
}

}  // namespace mcpe::world
