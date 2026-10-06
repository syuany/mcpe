#ifndef MCPE_PROTOCOL_CHUNK_SERIALIZER_H
#define MCPE_PROTOCOL_CHUNK_SERIALIZER_H

#include <memory>

#include "common/ByteBuffer.h"
#include "protocol/ClientSession.h"
#include "world/Chunk.h"

namespace mcpe::protocol {

class ChunkSerializer {
 public:
  /**
   * @brief 按照 Minecraft 1.12.2 (Protocol 340) 序列化并向客户端会话发送 0x20 区块数据包
   * @param session 目标客户端会话 (ClientSession)
   * @param chunk 包含方块与群系的世界领域对象
   * @param ground_up 是否全新连续加载（首次下发必须为 true，携带 256 字节 Biomes）
   */
  static void sendChunkData(
      const std::shared_ptr<protocol::ClientSession>& session,
      const world::Chunk& chunk,
      bool ground_up = true
  );

  /**
   * @brief 核心序列化逻辑：将 Chunk 转换为未加帧头的原始 0x20 数据包载荷 ByteBuffer
   */
  static ByteBuffer serialize(const world::Chunk& chunk, bool ground_up = true);
};

}  // namespace mcpe::protocol

#endif  // MCPE_PROTOCOL_CHUNK_SERIALIZER_H
