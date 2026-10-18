#include "protocol/ChunkSerializer.h"

#include <array>
#include <cstdint>

#include "network/PacketWriter.h"
#include "protocol/ClientSession.h"
#include "protocol/PacketTypes.h"
#include "world/Chunk.h"
#include "world/ChunkSection.h"

namespace mcpe::protocol {

ByteBuffer
ChunkSerializer::serialize(const world::Chunk& chunk, bool ground_up) {
  ByteBuffer buffer;
  const uint16_t bitmask = chunk.primaryBitMask();

  // 预编译期常驻光照模板（C++23 constexpr 在编译期填充，0 运行时初始化开销）
  static constexpr std::array<char, 2048> kBlockLight{0};
  static constexpr auto kSkyLight = [] {
    std::array<char, 2048> arr;
    arr.fill(static_cast<char>(0xFF));
    return arr;
  }();

  // 遍历 16 个 Section，若 BitMask 第 i 位为 1
  for (size_t i = 0; i < world::Chunk::kSectionCount; ++i) {
    if ((bitmask & (1 << i)) == 0) {
      continue;
    }
    const world::ChunkSection* section = chunk.getSection(i);
    if (!section) {
      continue;
    }

    // 1. Bits Per Block: 13 (直接模式使用全局调色板)
    buffer.writeBe<uint8_t>(static_cast<uint8_t>(world::ChunkSection::kBitsPerBlock));

    // 2. Dummy Palette Length: VarInt 0 (全局调色板直接模式固定为 0)
    buffer.appendVarInt(0);

    // 3. Data Array Length: VarInt 832 (832 个 64 位整数)
    buffer.appendVarInt(static_cast<int32_t>(world::ChunkSection::kDataLongs));

    // 4. Data Array: 832 个 uint64_t（按大端写入）
    std::array<uint64_t, world::ChunkSection::kDataLongs> longs{};
    section->packToBits13(longs);
    for (uint64_t val : longs) {
      buffer.writeBe<uint64_t>(val);
    }

    // 5. Block Light: 2048 字节 (全部 0x00)
    buffer.append(kBlockLight.data(), kBlockLight.size());

    // 6. Sky Light: 2048 字节 (全部 0xFF)
    buffer.append(kSkyLight.data(), kSkyLight.size());
  }

  // 若 ground_up == true，末尾追加 256 字节 Biomes
  if (ground_up) {
    buffer.append(chunk.biomes().data(), chunk.biomes().size());
  }

  return buffer;
}

void
ChunkSerializer::sendChunkData(
    const std::shared_ptr<protocol::ClientSession>& session,
    const world::Chunk& chunk,
    bool ground_up
) {
  if (!session) {
    return;
  }

  ByteBuffer data_payload = serialize(chunk, ground_up);

  network::PacketWriter<protocol::ClientSession> writer(
      session,
      protocol::ClientboundPacket::ChunkData
  );
  writer.writeBe<int32_t>(chunk.x());
  writer.writeBe<int32_t>(chunk.z());
  writer.writeBe<uint8_t>(ground_up ? 1 : 0);
  writer.appendVarInt(static_cast<int32_t>(chunk.primaryBitMask()));
  writer.appendVarInt(static_cast<int32_t>(data_payload.readableBytes()));
  writer.append(data_payload.peek(), data_payload.readableBytes());
  writer.appendVarInt(0);  // Number of block entities
}

void
ChunkSerializer::sendUnloadChunk(
    const std::shared_ptr<protocol::ClientSession>& session,
    world::ChunkPos pos
) {
  if (!session) {
    return;
  }

  network::PacketWriter<protocol::ClientSession> writer(
      session,
      protocol::ClientboundPacket::UnloadChunk
  );
  writer.writeBe<int32_t>(pos.x);
  writer.writeBe<int32_t>(pos.z);
}

}  // namespace mcpe::protocol
