#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "common/BufferPool.h"
#include "common/ByteBuffer.h"
#include "network/PacketFraming.h"
#include "network/SessionReadBuffer.h"

using namespace mcpe;
using namespace mcpe::network;

// 1. 验证“0 常驻内存”：无网络数据时严格不持有物理块
TEST(SessionReadBufferTest, ZeroResidentMemoryWhenIdle) {
  BufferPool pool(10);
  EXPECT_EQ(pool.allocatedCount(), 0);

  {
    SessionReadBuffer buf(pool);
    // 刚构造时，不借出任何物理块
    EXPECT_FALSE(buf.isHoldingBlock());
    EXPECT_EQ(buf.readableBytes(), 0);
    EXPECT_TRUE(buf.unconsumedView().empty());
    EXPECT_EQ(pool.allocatedCount(), 0);
  }

  EXPECT_EQ(pool.allocatedCount(), 0);
}

// 2. 惰性借池与全部消费后瞬时全量回收
TEST(SessionReadBufferTest, LazyAllocationAndInstantRecycle) {
  BufferPool pool(10);
  SessionReadBuffer buf(pool);

  // 1. 准备接收数据，惰性借出 4KB
  auto mutable_buf = buf.prepareWriteBuffer();
  EXPECT_TRUE(buf.isHoldingBlock());
  EXPECT_EQ(pool.allocatedCount(), 1);

  // 2. 模拟从内核写入 10 字节
  std::memcpy(mutable_buf.data(), "0123456789", 10);
  buf.commit(10);
  EXPECT_EQ(buf.readableBytes(), 10);
  EXPECT_EQ(buf.unconsumedView(), "0123456789");

  // 3. 消费 4 字节，剩余 6 字节未读，此时依然扣留块
  buf.consume(4);
  EXPECT_EQ(buf.readableBytes(), 6);
  EXPECT_EQ(buf.unconsumedView(), "456789");
  EXPECT_TRUE(buf.isHoldingBlock());
  EXPECT_EQ(pool.allocatedCount(), 1);

  // 4. 消费剩余 6 字节：数据全部被消费，内部立即触发 recycle 归还内存池！
  buf.consume(6);
  EXPECT_EQ(buf.readableBytes(), 0);
  EXPECT_FALSE(buf.isHoldingBlock());
  EXPECT_EQ(pool.allocatedCount(), 0); // 内存池已满血复原！
}

// 3. 结合 PacketFraming 流式切包联动与自动归还
TEST(SessionReadBufferTest, PacketFramingIntegrationAndRecycle) {
  BufferPool pool(10);
  SessionReadBuffer buf(pool);

  // 构造两个 Minecraft 协议格式数据包：[Length: VarInt] + [Payload]
  // 包 1: 长度 3, 内容 "ABC"
  // 包 2: 长度 5, 内容 "HELLO"
  ByteBuffer temp;
  temp.appendVarInt(3);
  temp.append("ABC");
  temp.appendVarInt(5);
  temp.append("HELLO");

  auto mutable_buf = buf.prepareWriteBuffer();
  std::memcpy(mutable_buf.data(), temp.peek(), temp.readableBytes());
  buf.commit(temp.readableBytes());

  // 第一次切包：切出 "ABC"
  auto res1 = PacketFraming::slicePacketFromView(buf.unconsumedView());
  ASSERT_TRUE(res1.has_value());
  EXPECT_EQ(res1->packet.toStringView(), "ABC");
  buf.consume(res1->consumed_bytes);

  // 包 1 切完后，由于还有包 2 未消费，物理块依然扣留
  EXPECT_TRUE(buf.isHoldingBlock());
  EXPECT_EQ(pool.allocatedCount(), 1);

  // 第二次切包：切出 "HELLO"
  auto res2 = PacketFraming::slicePacketFromView(buf.unconsumedView());
  ASSERT_TRUE(res2.has_value());
  EXPECT_EQ(res2->packet.toStringView(), "HELLO");
  buf.consume(res2->consumed_bytes);

  // 包 2 切完后，缓冲区数据全部消费完毕，瞬间自动回收！
  EXPECT_FALSE(buf.isHoldingBlock());
  EXPECT_EQ(pool.allocatedCount(), 0);
}

// 4. 半包场景就地滑动与对齐 (compactIfIncomplete)
TEST(SessionReadBufferTest, HalfPacketCompaction) {
  BufferPool pool(10);
  SessionReadBuffer buf(pool);

  // 模拟包 1 (已就绪并消费) + 半包 2 (包长 VarInt 为 6，但实际只到了 2 字节)
  ByteBuffer p1;
  p1.appendVarInt(3);
  p1.append("XYZ");

  ByteBuffer p2_half;
  p2_half.appendVarInt(6);
  p2_half.append("AB"); // 应该还要 4 字节才完整

  auto m_buf = buf.prepareWriteBuffer();
  std::memcpy(m_buf.data(), p1.peek(), p1.readableBytes());
  std::memcpy(static_cast<char*>(m_buf.data()) + p1.readableBytes(), p2_half.peek(), p2_half.readableBytes());
  buf.commit(p1.readableBytes() + p2_half.readableBytes());

  // 切出包 1
  auto r1 = PacketFraming::slicePacketFromView(buf.unconsumedView());
  ASSERT_TRUE(r1.has_value());
  buf.consume(r1->consumed_bytes);

  // 尝试切包 2 -> 遭遇半包 kIncomplete
  auto r2 = PacketFraming::slicePacketFromView(buf.unconsumedView());
  ASSERT_FALSE(r2.has_value());
  EXPECT_EQ(r2.error(), FramingError::kIncomplete);

  // 执行就地碎片整理
  buf.compactIfIncomplete();
  EXPECT_EQ(buf.readableBytes(), p2_half.readableBytes());

  // 模拟网络下一批数据到达（补齐剩余 4 字节 "CDEF"）
  auto m_buf2 = buf.prepareWriteBuffer();
  std::memcpy(m_buf2.data(), "CDEF", 4);
  buf.commit(4);

  // 再次切包 -> 完整切出包 2！
  auto r2_complete = PacketFraming::slicePacketFromView(buf.unconsumedView());
  ASSERT_TRUE(r2_complete.has_value());
  EXPECT_EQ(r2_complete->packet.toStringView(), "ABCDEF");
  buf.consume(r2_complete->consumed_bytes);

  // 最终全部归还
  EXPECT_FALSE(buf.isHoldingBlock());
  EXPECT_EQ(pool.allocatedCount(), 0);
}

// 5. 遭遇单包超过 4KB 的超限包平滑伸缩
TEST(SessionReadBufferTest, OversizedPacketHandling) {
  BufferPool pool(10);
  SessionReadBuffer buf(pool);

  // 构造一个 5000 字节的超大包
  std::string huge_payload(5000, 'Z');
  ByteBuffer huge_buf;
  huge_buf.appendVarInt(5000);
  huge_buf.append(huge_payload);

  size_t total_written = 0;
  const char* src = huge_buf.peek();
  size_t total_size = huge_buf.readableBytes();

  // 循环写入
  while (total_written < total_size) {
    auto m_buf = buf.prepareWriteBuffer();
    size_t chunk = std::min(m_buf.size(), total_size - total_written);
    std::memcpy(m_buf.data(), src + total_written, chunk);
    buf.commit(chunk);
    total_written += chunk;
  }

  // 切出超大包
  auto res = PacketFraming::slicePacketFromView(buf.unconsumedView());
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->packet.readableBytes(), 5000);
  EXPECT_EQ(res->packet.toStringView(), huge_payload);
  buf.consume(res->consumed_bytes);

  // 消费完成后重置
  EXPECT_FALSE(buf.isHoldingBlock());
  EXPECT_EQ(pool.allocatedCount(), 0);
}
