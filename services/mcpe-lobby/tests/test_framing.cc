/**
 * @file test_framing.cc
 * @brief 传输层拆包器（Packet Framing）单元测试
 *
 * 覆盖 Minecraft 协议网络流切包的核心场景：
 * 1. 完整单个数据包即时切出
 * 2. TCP 粘包（一次收到多个包，连环切包）
 * 3. TCP 半包（分批陆续到达，零副作用等待）
 * 4. Netty 零拷贝切片验证（底层 Storage 内存共享与生命周期）
 * 5. 粘包场景下多个切片并发持有与解码验证
 * 6. Copy-On-Write 写时复制内存隔离保护验证
 */

#include <gtest/gtest.h>

#include "network/PacketFraming.h"

using namespace mcpe;
using namespace mcpe::network;

// 测试 1：完整单包直接切出
TEST(PacketFramingTest, SingleCompletePacket) {
  ByteBuffer buf;
  // 构造包：Length=3, ID=0x00, Payload="AB"
  buf.appendVarInt(3);
  buf.appendByte(0x00);
  buf.append("AB", 2);

  auto res = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->readableBytes(), 3);
  EXPECT_TRUE(res->isSlice());
  EXPECT_EQ(buf.readableBytes(), 0);  // 缓冲区已被恰好消费完
}

// 测试 2：TCP 粘包（两个包拼在一起，连续切出 2 个包）
TEST(PacketFramingTest, StickingPackets) {
  ByteBuffer buf;
  // 包 1：Length=2, Payload="P1"
  buf.appendVarInt(2);
  buf.append("P1", 2);
  // 包 2：Length=2, Payload="P2"
  buf.appendVarInt(2);
  buf.append("P2", 2);

  auto p1 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(p1.has_value());
  EXPECT_TRUE(p1->isSlice());
  EXPECT_EQ(p1->retrieveAllAsString(), "P1");

  auto p2 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(p2.has_value());
  EXPECT_TRUE(p2->isSlice());
  EXPECT_EQ(p2->retrieveAllAsString(), "P2");

  EXPECT_EQ(buf.readableBytes(), 0);
}

// 测试 3：TCP 半包（分批喂入，最后一次才返回包）
TEST(PacketFramingTest, TruncatedHalfPacket) {
  ByteBuffer buf;

  // 模拟包总长 5 字节
  // 第 1 次：只喂入了包长 VarInt (1 字节)
  buf.appendVarInt(4);
  auto r1 = PacketFraming::slicePacket(buf);
  EXPECT_EQ(r1.error(), FramingError::kIncomplete);
  EXPECT_EQ(buf.readableBytes(), 1);  // 游标绝不回退或乱滑

  // 第 2 次：喂入 2 字节载荷（仍不够 4 字节）
  buf.append("AB", 2);
  auto r2 = PacketFraming::slicePacket(buf);
  EXPECT_EQ(r2.error(), FramingError::kIncomplete);

  // 第 3 次：补全最后 2 字节
  buf.append("CD", 2);
  auto r3 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(r3.has_value());
  EXPECT_EQ(r3->retrieveAllAsString(), "ABCD");
  EXPECT_EQ(buf.readableBytes(), 0);
}

// 测试 4：Netty 风格零拷贝切片（底层 Storage 内存共享与引用计数）
TEST(PacketFramingTest, ZeroCopySlicingSharesMemory) {
  ByteBuffer buf;
  buf.appendVarInt(9);
  buf.appendVarInt(0x01);
  buf.writeBe<int64_t>(123456789LL);

  const char* original_payload_addr = buf.peek() + 1;  // 跳过包长 VarInt(1字节)

  auto res = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(res.has_value());
  EXPECT_TRUE(res->isSlice());
  EXPECT_EQ(res->readableBytes(), 9);
  // 切片首地址应精准对齐原缓冲区中的有效载荷地址，实现真正的零拷贝
  EXPECT_EQ(res->peek(), original_payload_addr);
  // 底层 Storage 正由 buf 与 slice 共同持有
  EXPECT_GE(res->useCount(), 2);

  // 切出的包本身就是标准 ByteBuffer，直接流式解码
  EXPECT_EQ(*res->readVarInt(), 0x01);
  EXPECT_EQ(*res->readBe<int64_t>(), 123456789LL);
  EXPECT_EQ(res->readableBytes(), 0);
}

// 测试 5：粘包场景下多个切片并发持有与解码验证
TEST(PacketFramingTest, ZeroCopyStickingPacketsConcurrentSlices) {
  ByteBuffer buf;
  // 包 1：Ping 0x01 + 8 字节大端整数
  buf.appendVarInt(9);
  buf.appendVarInt(0x01);
  buf.writeBe<int64_t>(123456789LL);

  // 包 2：TeleportConfirm 0x00 + VarInt 42
  buf.appendVarInt(2);
  buf.appendVarInt(0x00);
  buf.appendVarInt(42);

  // 1. 连续切出两个包
  auto p1 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(p1.has_value());
  auto p2 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(p2.has_value());

  // 两个包和父缓冲区共享底层 Storage (useCount == 3)
  EXPECT_EQ(p1->useCount(), 3);
  EXPECT_EQ(p2->useCount(), 3);

  // 解码包 1
  EXPECT_EQ(*p1->readVarInt(), 0x01);
  EXPECT_EQ(*p1->readBe<int64_t>(), 123456789LL);
  EXPECT_EQ(p1->readableBytes(), 0);

  // 解码包 2
  EXPECT_EQ(*p2->readVarInt(), 0x00);
  EXPECT_EQ(*p2->readVarInt(), 42);
  EXPECT_EQ(p2->readableBytes(), 0);

  EXPECT_EQ(buf.readableBytes(), 0);
}

// 测试 6：Copy-On-Write 写时复制安全隔离（在途切片不受后续网络写入影响）
TEST(PacketFramingTest, CopyOnWriteIsolationProtectsActiveSlices) {
  ByteBuffer buf;
  // 喂入包 1
  buf.appendVarInt(10);
  buf.append("PACKET_ONE", 10);

  // 切出包 1（进入在途消费状态）
  auto slice1_res = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(slice1_res.has_value());
  ByteBuffer slice1 = std::move(*slice1_res);
  EXPECT_TRUE(slice1.isSlice());
  EXPECT_EQ(slice1.useCount(), 2);

  // 模拟后续网络 I/O 到达：写入包 2
  // 由于底层 Storage 正在被 slice1 共享，buf 写入时触发 COW，分配独立 Storage
  buf.appendVarInt(10);
  buf.append("PACKET_TWO", 10);

  // slice1 的内存受 COW 隔离保护，内容未被篡改
  EXPECT_EQ(slice1.retrieveAllAsString(), "PACKET_ONE");

  // 切出包 2
  auto slice2_res = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(slice2_res.has_value());
  ByteBuffer slice2 = std::move(*slice2_res);
  EXPECT_EQ(slice2.retrieveAllAsString(), "PACKET_TWO");
}
