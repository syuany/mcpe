/**
 * @file test_framing.cc
 * @brief 传输层拆包器（Packet Framing）单元测试
 *
 * 覆盖 Minecraft 协议网络流切包的核心场景：
 * 1. 完整单个数据包即时切出
 * 2. TCP 粘包（一次收到多个包，连环切包）
 * 3. TCP 半包（分批陆续到达，零副作用等待）
 */

#include <gtest/gtest.h>

#include "common/ByteReader.h"
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
  EXPECT_EQ(p1->retrieveAllAsString(), "P1");

  auto p2 = PacketFraming::slicePacket(buf);
  ASSERT_TRUE(p2.has_value());
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

// 测试 4：零拷贝切包视图（slicePacketView 纯零拷贝）
TEST(PacketFramingTest, ZeroCopySlicePacketView) {
  ByteBuffer buf;
  buf.appendVarInt(5);
  buf.appendByte(0x01);
  buf.append("TEST", 4);

  auto view_res = PacketFraming::slicePacketView(buf);
  ASSERT_TRUE(view_res.has_value());
  EXPECT_EQ(view_res->size(), 5);
  EXPECT_EQ(view_res->front(), '\x01');
  EXPECT_EQ(view_res->substr(1), "TEST");
  EXPECT_EQ(buf.readableBytes(), 0);  // 接收缓冲区游标已精准划过
}

// 测试 5：零拷贝切包粘包处理与 ByteReader 解码
TEST(PacketFramingTest, ZeroCopyStickingPacketViewsAndByteReader) {
  ByteBuffer buf;
  // 包 1：Ping 0x01 + 8 字节大端整数
  buf.appendVarInt(9);
  buf.appendVarInt(0x01);
  buf.writeBe<int64_t>(123456789LL);

  // 包 2：TeleportConfirm 0x00 + VarInt 42
  buf.appendVarInt(2);
  buf.appendVarInt(0x00);
  buf.appendVarInt(42);

  // 1. 零拷贝切出包 1
  auto v1 = PacketFraming::slicePacketView(buf);
  ASSERT_TRUE(v1.has_value());
  ByteReader reader1(*v1);
  EXPECT_EQ(*reader1.readVarInt(), 0x01);
  EXPECT_EQ(*reader1.readBe<int64_t>(), 123456789LL);
  EXPECT_EQ(reader1.readableBytes(), 0);

  // 2. 零拷贝切出包 2
  auto v2 = PacketFraming::slicePacketView(buf);
  ASSERT_TRUE(v2.has_value());
  ByteReader reader2(*v2);
  EXPECT_EQ(*reader2.readVarInt(), 0x00);
  EXPECT_EQ(*reader2.readVarInt(), 42);
  EXPECT_EQ(reader2.readableBytes(), 0);

  EXPECT_EQ(buf.readableBytes(), 0);
}

// 测试 6：零拷贝切包视图半包无副作用
TEST(PacketFramingTest, ZeroCopyHalfPacketNoSideEffect) {
  ByteBuffer buf;
  buf.appendVarInt(10);
  buf.append("123", 3);

  auto v = PacketFraming::slicePacketView(buf);
  ASSERT_FALSE(v.has_value());
  EXPECT_EQ(v.error(), FramingError::kIncomplete);
  EXPECT_EQ(buf.readableBytes(), 4);  // 读游标未发生偏移
}

