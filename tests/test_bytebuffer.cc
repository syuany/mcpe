#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "mcpe/common/ByteBuffer.h"

using mcpe::ByteBuffer;

// ---------------------------------------------------------------------------
// 基础：初始状态与字符串读写
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, InitiallyEmpty) {
  ByteBuffer buf;
  EXPECT_EQ(buf.readableBytes(), 0u);
  EXPECT_TRUE(buf.toStringView().empty());
  EXPECT_TRUE(buf.toSpan().empty());
}

TEST(ByteBufferTest, AppendStringAndRetrieveAll) {
  ByteBuffer buf;
  buf.append("hello world");
  EXPECT_EQ(buf.readableBytes(), 11u);
  EXPECT_EQ(buf.retrieveAllAsString(), "hello world");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferTest, AppendRawBytes) {
  ByteBuffer buf;
  const char data[] = {0x01, 0x02, 0x03, 0x04};
  buf.append(data, sizeof(data));
  EXPECT_EQ(buf.readableBytes(), 4u);
  EXPECT_EQ(buf.toStringView(), std::string_view(data, 4));

  buf.retrieve(2);
  EXPECT_EQ(buf.readableBytes(), 2u);
  EXPECT_EQ(buf.retrieveAllAsString(), std::string("\x03\x04", 2));
}

TEST(ByteBufferTest, RetrieveAsString) {
  ByteBuffer buf;
  buf.append("abcdef");
  EXPECT_EQ(buf.retrieveAsString(3), "abc");
  EXPECT_EQ(buf.readableBytes(), 3u);
  EXPECT_EQ(buf.retrieveAllAsString(), "def");
}

// ---------------------------------------------------------------------------
// 整型：大端序字节布局 + 往返
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, AppendInt8) {
  ByteBuffer buf;
  buf.appendInt8(0x7F);
  EXPECT_EQ(buf.readableBytes(), 1u);
  EXPECT_EQ(buf.peekInt8(), 0x7F);
  EXPECT_EQ(buf.readInt8(), 0x7F);
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferTest, AppendInt16BigEndian) {
  ByteBuffer buf;
  buf.appendInt16(0x1234);
  auto v = buf.toStringView();
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(static_cast<unsigned char>(v[0]), 0x12u);
  EXPECT_EQ(static_cast<unsigned char>(v[1]), 0x34u);
  EXPECT_EQ(buf.readInt16(), 0x1234);
}

TEST(ByteBufferTest, AppendInt32BigEndian) {
  ByteBuffer buf;
  buf.appendInt32(0x12345678);
  auto v = buf.toStringView();
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(static_cast<unsigned char>(v[0]), 0x12u);
  EXPECT_EQ(static_cast<unsigned char>(v[1]), 0x34u);
  EXPECT_EQ(static_cast<unsigned char>(v[2]), 0x56u);
  EXPECT_EQ(static_cast<unsigned char>(v[3]), 0x78u);
  EXPECT_EQ(buf.readInt32(), 0x12345678);
}

TEST(ByteBufferTest, AppendInt64BigEndian) {
  ByteBuffer buf;
  buf.appendInt64(0x0102030405060708LL);
  auto v = buf.toStringView();
  ASSERT_EQ(v.size(), 8u);
  EXPECT_EQ(static_cast<unsigned char>(v[0]), 0x01u);
  EXPECT_EQ(static_cast<unsigned char>(v[7]), 0x08u);
  EXPECT_EQ(buf.readInt64(), 0x0102030405060708LL);
}

TEST(ByteBufferTest, NegativeIntegers) {
  ByteBuffer buf;
  buf.appendInt8(-1);
  buf.appendInt16(-2);
  buf.appendInt32(-3);
  buf.appendInt64(-4);

  EXPECT_EQ(buf.readInt8(), -1);
  EXPECT_EQ(buf.readInt16(), -2);
  EXPECT_EQ(buf.readInt32(), -3);
  EXPECT_EQ(buf.readInt64(), -4);
}

TEST(ByteBufferTest, IntegerLimits) {
  ByteBuffer buf;
  buf.appendInt32(std::numeric_limits<int32_t>::min());
  buf.appendInt32(std::numeric_limits<int32_t>::max());
  buf.appendInt64(std::numeric_limits<int64_t>::min());
  buf.appendInt64(std::numeric_limits<int64_t>::max());

  EXPECT_EQ(buf.readInt32(), std::numeric_limits<int32_t>::min());
  EXPECT_EQ(buf.readInt32(), std::numeric_limits<int32_t>::max());
  EXPECT_EQ(buf.readInt64(), std::numeric_limits<int64_t>::min());
  EXPECT_EQ(buf.readInt64(), std::numeric_limits<int64_t>::max());
}

// ---------------------------------------------------------------------------
// 浮点：位模式按大端序存储，往返应精确
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, FloatRoundTrip) {
  ByteBuffer buf;
  const float values[] = {0.0f, 1.0f, -1.5f, 3.14159f, std::numeric_limits<float>::infinity()};
  for (float v : values) buf.appendFloat(v);
  for (float v : values) EXPECT_EQ(buf.readFloat(), v);
}

TEST(ByteBufferTest, DoubleRoundTrip) {
  ByteBuffer buf;
  const double values[] = {0.0, 1.0, -1.5, 3.141592653589793,
                           std::numeric_limits<double>::infinity()};
  for (double v : values) buf.appendDouble(v);
  for (double v : values) EXPECT_EQ(buf.readDouble(), v);
}

// ---------------------------------------------------------------------------
// peek / read 的语义
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, PeekDoesNotAdvance) {
  ByteBuffer buf;
  buf.appendInt32(42);
  EXPECT_EQ(buf.peekInt32(), 42);
  EXPECT_EQ(buf.peekInt32(), 42);
  EXPECT_EQ(buf.readableBytes(), 4u);

  EXPECT_EQ(buf.readInt32(), 42);
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferTest, MixedAppendAndRead) {
  ByteBuffer buf;
  buf.append("AB");
  buf.appendInt32(0xDEADBEEF);
  buf.append("CD");
  EXPECT_EQ(buf.readableBytes(), 2u + 4u + 2u);

  EXPECT_EQ(buf.retrieveAsString(2), "AB");
  EXPECT_EQ(buf.readInt32(), static_cast<int32_t>(0xDEADBEEF));
  EXPECT_EQ(buf.retrieveAllAsString(), "CD");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

// ---------------------------------------------------------------------------
// 索引重置 / retrieve / retrieveUntil
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, RetrieveAllResetsIndices) {
  ByteBuffer buf;
  buf.append("hello");
  EXPECT_EQ(buf.retrieveAllAsString(), "hello");
  EXPECT_EQ(buf.readableBytes(), 0u);

  // 重置后应能继续追加
  buf.append("world");
  EXPECT_EQ(buf.retrieveAllAsString(), "world");
}

TEST(ByteBufferTest, RetrieveUntil) {
  ByteBuffer buf;
  buf.append("hello world");
  const char* end = buf.peek() + 5;
  buf.retrieveUntil(end);
  EXPECT_EQ(buf.retrieveAllAsString(), " world");
}

TEST(ByteBufferTest, ReadOnEmptyClearsBuffer) {
  ByteBuffer buf;
  buf.append("xyz");
  buf.readableBytes();  // no-op
  buf.retrieve(3);
  EXPECT_EQ(buf.readableBytes(), 0u);

  // 再次 append 应正常工作
  buf.appendInt16(0x0102);
  EXPECT_EQ(buf.readInt16(), 0x0102);
}

// ---------------------------------------------------------------------------
// prepend / shrink / swap
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, Prepend) {
  ByteBuffer buf;
  buf.append("world");
  const char prefix[] = "hello ";
  buf.prepend(prefix, sizeof(prefix) - 1);
  EXPECT_EQ(buf.retrieveAllAsString(), "hello world");
}

TEST(ByteBufferTest, ShrinkPreservesContents) {
  ByteBuffer buf;
  buf.append("hello");
  buf.appendInt32(1234);

  buf.shrink(0);
  EXPECT_EQ(buf.retrieveAsString(5), "hello");
  EXPECT_EQ(buf.readInt32(), 1234);
}

TEST(ByteBufferTest, Swap) {
  ByteBuffer a, b;
  a.append("aaa");
  b.append("bbbb");
  a.swap(b);
  EXPECT_EQ(a.retrieveAllAsString(), "bbbb");
  EXPECT_EQ(b.retrieveAllAsString(), "aaa");
}

// ---------------------------------------------------------------------------
// 扩容：大量写入 / 循环读写
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, LargeAppendForcesGrowth) {
  ByteBuffer buf;
  std::string data(100000, 'x');
  buf.append(data);
  EXPECT_EQ(buf.readableBytes(), data.size());
  EXPECT_EQ(buf.retrieveAllAsString(), data);
}

TEST(ByteBufferTest, RepeatedAppendAndRetrieve) {
  ByteBuffer buf;
  for (int i = 0; i < 1000; ++i) {
    buf.appendInt32(i);
    EXPECT_EQ(buf.readInt32(), i);
  }
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferTest, AppendAfterPartialReadReusesSpace) {
  ByteBuffer buf;
  buf.append("0123456789");
  buf.retrieve(5);           // 留下 "56789"
  buf.append("abcdefghij");  // 触发 makeSpace
  EXPECT_EQ(buf.retrieveAllAsString(), "56789abcdefghij");
}

// ---------------------------------------------------------------------------
// 视图接口
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, ToSpan) {
  ByteBuffer buf;
  buf.append("abc");
  auto sp = buf.toSpan();
  ASSERT_EQ(sp.size(), 3u);
  EXPECT_EQ(sp[0], 'a');
  EXPECT_EQ(sp[1], 'b');
  EXPECT_EQ(sp[2], 'c');
}

TEST(ByteBufferTest, ToStringView) {
  ByteBuffer buf;
  buf.append("view");
  std::string_view sv = buf.toStringView();
  EXPECT_EQ(sv, "view");
  EXPECT_EQ(sv.size(), 4u);
}