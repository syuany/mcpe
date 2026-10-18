#ifndef MCPE_WORLD_FLAT_WORLD_GENERATOR_H
#define MCPE_WORLD_FLAT_WORLD_GENERATOR_H

#include "world/Chunk.h"

namespace mcpe::world {

class FlatWorldGenerator {
 public:
  FlatWorldGenerator() = default;

  /**
   * @brief 在传入的空白 Chunk 中填充经典 4 层超平坦方块：
   * Y=0: 基岩 (Bedrock)
   * Y=1~2: 泥土 (Dirt)
   * Y=3: 草方块 (Grass)
   * Y>=4: 保持空气 (无需填充，惰性节省内存)
   */
  void generate(Chunk& chunk);
};

}  // namespace mcpe::world

#endif  // MCPE_WORLD_FLAT_WORLD_GENERATOR_H
