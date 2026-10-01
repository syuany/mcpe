#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <string_view>

#include "common/ByteBuffer.h"

using mcpe::ByteBuffer;
using mcpe::DecodeError;

void PrintTo(DecodeError e, std::ostream* os) { *os << mcpe::toString(e); }

namespace {

// 解析失败：错误分类必须精确匹配
template <typename T>
void expectDecodeError(const std::expected<T, DecodeError>& result, DecodeError expected,
                       const char* what) {
  if (result.has_value()) {
    ADD_FAILURE() << what << " 期望失败，却解析成功了: " << *result;
    return;
  }
  EXPECT_EQ(result.error(), expected) << what;
}

// 解析成功：返回值必须相等
template <typename T, typename U>
void expectDecodeValue(const std::expected<T, DecodeError>& result, U expected, const char* what) {
  if (!result.has_value()) {
    ADD_FAILURE() << what << " 期望成功，实际失败: " << mcpe::toString(result.error());
    return;
  }
  EXPECT_EQ(*result, static_cast<T>(expected)) << what;
}

}  // namespace

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
// 整型：大端序字节布局 + 往返 (通过泛型 writeBe / readBe)
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, AppendInt8) {
  ByteBuffer buf;
  buf.writeBe<int8_t>(0x7F);
  EXPECT_EQ(buf.readableBytes(), 1u);
  EXPECT_EQ(*buf.peekBe<int8_t>(), 0x7F);
  EXPECT_EQ(*buf.readBe<int8_t>(), 0x7F);
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferTest, AppendInt16BigEndian) {
  ByteBuffer buf;
  buf.writeBe<int16_t>(0x1234);
  auto v = buf.toStringView();
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(static_cast<unsigned char>(v[0]), 0x12u);
  EXPECT_EQ(static_cast<unsigned char>(v[1]), 0x34u);
  EXPECT_EQ(*buf.readBe<int16_t>(), 0x1234);
}

TEST(ByteBufferTest, AppendInt32BigEndian) {
  ByteBuffer buf;
  buf.writeBe<int32_t>(0x12345678);
  auto v = buf.toStringView();
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(static_cast<unsigned char>(v[0]), 0x12u);
  EXPECT_EQ(static_cast<unsigned char>(v[1]), 0x34u);
  EXPECT_EQ(static_cast<unsigned char>(v[2]), 0x56u);
  EXPECT_EQ(static_cast<unsigned char>(v[3]), 0x78u);
  EXPECT_EQ(*buf.readBe<int32_t>(), 0x12345678);
}

TEST(ByteBufferTest, AppendInt64BigEndian) {
  ByteBuffer buf;
  buf.writeBe<int64_t>(0x0102030405060708LL);
  auto v = buf.toStringView();
  ASSERT_EQ(v.size(), 8u);
  EXPECT_EQ(static_cast<unsigned char>(v[0]), 0x01u);
  EXPECT_EQ(static_cast<unsigned char>(v[7]), 0x08u);
  EXPECT_EQ(*buf.readBe<int64_t>(), 0x0102030405060708LL);
}

TEST(ByteBufferTest, NegativeIntegers) {
  ByteBuffer buf;
  buf.writeBe<int8_t>(-1);
  buf.writeBe<int16_t>(-2);
  buf.writeBe<int32_t>(-3);
  buf.writeBe<int64_t>(-4);

  EXPECT_EQ(*buf.readBe<int8_t>(), -1);
  EXPECT_EQ(*buf.readBe<int16_t>(), -2);
  EXPECT_EQ(*buf.readBe<int32_t>(), -3);
  EXPECT_EQ(*buf.readBe<int64_t>(), -4);
}

TEST(ByteBufferTest, IntegerLimits) {
  ByteBuffer buf;
  buf.writeBe<int32_t>(std::numeric_limits<int32_t>::min());
  buf.writeBe<int32_t>(std::numeric_limits<int32_t>::max());
  buf.writeBe<int64_t>(std::numeric_limits<int64_t>::min());
  buf.writeBe<int64_t>(std::numeric_limits<int64_t>::max());

  EXPECT_EQ(*buf.readBe<int32_t>(), std::numeric_limits<int32_t>::min());
  EXPECT_EQ(*buf.readBe<int32_t>(), std::numeric_limits<int32_t>::max());
  EXPECT_EQ(*buf.readBe<int64_t>(), std::numeric_limits<int64_t>::min());
  EXPECT_EQ(*buf.readBe<int64_t>(), std::numeric_limits<int64_t>::max());
}

// ---------------------------------------------------------------------------
// 浮点：位模式按大端序存储，往返应精确
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, FloatRoundTrip) {
  ByteBuffer buf;
  const float values[] = {0.0f, 1.0f, -1.5f, 3.14159f, std::numeric_limits<float>::infinity()};
  for (float v : values) buf.writeBe<float>(v);
  for (float v : values) EXPECT_EQ(*buf.readBe<float>(), v);
}

TEST(ByteBufferTest, DoubleRoundTrip) {
  ByteBuffer buf;
  const double values[] = {0.0, 1.0, -1.5, 3.141592653589793,
                           std::numeric_limits<double>::infinity()};
  for (double v : values) buf.writeBe<double>(v);
  for (double v : values) EXPECT_EQ(*buf.readBe<double>(), v);
}

// ---------------------------------------------------------------------------
// peek / read 的语义
// ---------------------------------------------------------------------------

TEST(ByteBufferTest, PeekDoesNotAdvance) {
  ByteBuffer buf;
  buf.writeBe<int32_t>(42);
  EXPECT_EQ(*buf.peekBe<int32_t>(), 42);
  EXPECT_EQ(*buf.peekBe<int32_t>(), 42);
  EXPECT_EQ(buf.readableBytes(), 4u);

  EXPECT_EQ(*buf.readBe<int32_t>(), 42);
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferTest, MixedAppendAndRead) {
  ByteBuffer buf;
  buf.append("AB");
  buf.writeBe<int32_t>(0xDEADBEEF);
  buf.append("CD");
  EXPECT_EQ(buf.readableBytes(), 2u + 4u + 2u);

  EXPECT_EQ(buf.retrieveAsString(2), "AB");
  EXPECT_EQ(*buf.readBe<int32_t>(), static_cast<int32_t>(0xDEADBEEF));
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
  buf.writeBe<int16_t>(0x0102);
  EXPECT_EQ(*buf.readBe<int16_t>(), 0x0102);
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
  buf.writeBe<int32_t>(1234);

  buf.shrink(0);
  EXPECT_EQ(buf.retrieveAsString(5), "hello");
  EXPECT_EQ(*buf.readBe<int32_t>(), 1234);
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
    buf.writeBe<int32_t>(i);
    EXPECT_EQ(*buf.readBe<int32_t>(), i);
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
// 扩容路径回归测试
//
// 背景：makeSpace 的判定条件曾误用 readableBytes() 而非 writableBytes()，
// 导致"缓冲区写满、且尚未消费任何数据"时走进错误的整理分支：
//   Debug   —— assert(kCheapPrepend < readerIndex_) 失败，进程直接 abort
//   Release —— 整理实际无效，却仍按原偏移写入 → heap-buffer-overflow
//              （ASan 实测：WRITE of size 1 at ByteBuffer::append）
// 下列用例是当时的最小复现，必须常驻，防止回归。
// ---------------------------------------------------------------------------

TEST(ByteBufferGrowthTest, GrowWhenCompletelyFullAndNothingConsumed) {
  ByteBuffer buf;
  const std::string full(ByteBuffer::kInitialSize, 'A');
  buf.append(full);
  ASSERT_EQ(buf.writableBytes(), 0u) << "前置条件：缓冲区已被写满";

  buf.append("B");  // ← 曾经在这里 abort / 堆越界写

  EXPECT_EQ(buf.readableBytes(), ByteBuffer::kInitialSize + 1);
  EXPECT_EQ(std::string(buf.toStringView().substr(0, ByteBuffer::kInitialSize)), full);
  EXPECT_EQ(buf.toStringView().back(), 'B');
}

TEST(ByteBufferGrowthTest, GrowAcrossManyCapacitySteps) {
  ByteBuffer buf;
  std::string expected;
  for (int i = 0; i < 5000; ++i) {
    const std::string chunk = std::to_string(i) + ",";
    expected += chunk;
    buf.append(chunk);
  }
  EXPECT_EQ(buf.readableBytes(), expected.size());
  EXPECT_EQ(buf.retrieveAllAsString(), expected);
}

TEST(ByteBufferGrowthTest, CompactionPreservesOrderWhenPartiallyConsumed) {
  ByteBuffer buf;
  buf.append(std::string(ByteBuffer::kInitialSize, 'X'));
  buf.retrieve(900);  // 消费大部分，制造可复用的头部空间
  const std::string tail = "TAIL";

  buf.append(tail);  // 触发整理（或扩容）

  EXPECT_EQ(buf.readableBytes(), ByteBuffer::kInitialSize - 900 + tail.size());
  EXPECT_EQ(buf.retrieveAllAsString(), std::string(ByteBuffer::kInitialSize - 900, 'X') + tail);
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

// ---------------------------------------------------------------------------
// 解析 API 分层：① 谓词层 hasBytes（bool 唯一恰当的用武之地）
// ---------------------------------------------------------------------------

TEST(ByteBufferPredicateTest, HasBytesIsTheCheapPredicate) {
  ByteBuffer buf;
  EXPECT_TRUE(buf.hasBytes(0)) << "0 字节永远算够";
  EXPECT_FALSE(buf.hasBytes(1));

  buf.append("abc");
  EXPECT_TRUE(buf.hasBytes(3));
  EXPECT_FALSE(buf.hasBytes(4));

  buf.retrieve(2);
  EXPECT_TRUE(buf.hasBytes(1));
  EXPECT_FALSE(buf.hasBytes(2));
}

TEST(ByteBufferPredicateTest, HasBytesDoesNotConsume) {
  ByteBuffer buf;
  buf.append("abc");
  EXPECT_TRUE(buf.hasBytes(3));
  EXPECT_TRUE(buf.hasBytes(3));
  EXPECT_EQ(buf.readableBytes(), 3u) << "谓词不得消费字节";
}

// ---------------------------------------------------------------------------
// 解析 API 分层：② 解析层 readBe*（定宽类型只可能 kIncomplete）
// ---------------------------------------------------------------------------

TEST(ByteBufferDecodeTest, EmptyBufferReportsIncompleteWithoutConsuming) {
  ByteBuffer buf;
  expectDecodeError(buf.readBe<int64_t>(), DecodeError::kIncomplete, "int64");
  expectDecodeError(buf.readBe<int32_t>(), DecodeError::kIncomplete, "int32");
  expectDecodeError(buf.readBe<int16_t>(), DecodeError::kIncomplete, "int16");
  expectDecodeError(buf.readBe<int8_t>(), DecodeError::kIncomplete, "int8");
  expectDecodeError(buf.readBe<uint8_t>(), DecodeError::kIncomplete, "byte");
  expectDecodeError(buf.readBe<float>(), DecodeError::kIncomplete, "float");
  expectDecodeError(buf.readBe<double>(), DecodeError::kIncomplete, "double");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferDecodeTest, ShortByOneByteReportsIncompleteWithoutConsuming) {
  // 半包：每个类型都少 1 个字节
  struct {
    int width;
    const char* name;
  } cases[] = {{8, "int64"}, {4, "int32"}, {2, "int16"}};
  for (const auto& c : cases) {
    ByteBuffer buf;
    for (int i = 0; i < c.width - 1; ++i) buf.appendByte(0x11);
    ASSERT_EQ(buf.readableBytes(), static_cast<size_t>(c.width - 1));

    if (c.width == 8) {
      expectDecodeError(buf.readBe<int64_t>(), DecodeError::kIncomplete, c.name);
    } else if (c.width == 4) {
      expectDecodeError(buf.readBe<int32_t>(), DecodeError::kIncomplete, c.name);
    } else {
      expectDecodeError(buf.readBe<int16_t>(), DecodeError::kIncomplete, c.name);
    }
    EXPECT_EQ(buf.readableBytes(), static_cast<size_t>(c.width - 1))
        << c.name << " 失败时不得消费任何字节";
  }
}

TEST(ByteBufferDecodeTest, HalfPacketThenRemainderArrives) {
  ByteBuffer buf;
  buf.appendByte(0x12);  // int32 的第一个字节

  expectDecodeError(buf.readBe<int32_t>(), DecodeError::kIncomplete, "收到 1/4 字节");

  buf.appendByte(0x34);
  buf.appendByte(0x56);
  buf.appendByte(0x78);  // 剩余 3 字节到齐

  expectDecodeValue(buf.readBe<int32_t>(), 0x12345678, "补齐后原地重试");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferDecodeTest, SuccessMatchesExpectedAndConsumesExactly) {
  ByteBuffer buf;
  buf.writeBe<int64_t>(-1234567890123LL);
  buf.writeBe<int32_t>(-123456);
  buf.writeBe<int16_t>(-1234);
  buf.writeBe<int8_t>(-12);
  buf.appendByte(0xAB);
  buf.writeBe<float>(1.5f);
  buf.writeBe<double>(-2.25);
  buf.append("TAIL");  // 哨兵：确认没有多吃

  expectDecodeValue(buf.readBe<int64_t>(), -1234567890123LL, "readBe<int64_t>");
  expectDecodeValue(buf.readBe<int32_t>(), -123456, "readBe<int32_t>");
  expectDecodeValue(buf.readBe<int16_t>(), -1234, "readBe<int16_t>");
  expectDecodeValue(buf.readBe<int8_t>(), -12, "readBe<int8_t>");
  expectDecodeValue(buf.readBe<uint8_t>(), 0xAB, "readBe<uint8_t>");
  expectDecodeValue(buf.readBe<float>(), 1.5f, "readBe<float>");
  expectDecodeValue(buf.readBe<double>(), -2.25, "readBe<double>");

  EXPECT_EQ(buf.readableBytes(), 4u) << "只应剩下哨兵";
  EXPECT_EQ(buf.retrieveAllAsString(), "TAIL");
}

TEST(ByteBufferDecodeTest, FailedParseCarriesNoValue) {
  ByteBuffer buf;  // 空
  auto r = buf.readBe<int32_t>();
  ASSERT_FALSE(r.has_value());
  // value_or 是安全取值方式；不要用 r.value()（错误时会抛）
  EXPECT_EQ(r.value_or(-42), -42);
}

// ---------------------------------------------------------------------------
// 现代 C++ 泛型 readBe/writeBe 及无符号数测试
// ---------------------------------------------------------------------------

TEST(ByteBufferGenericTest, UnsignedTypesRoundTrip) {
  ByteBuffer buf;
  buf.writeBe<uint16_t>(25565);  // Minecraft 标准服务器端口
  buf.writeBe<uint32_t>(0xDEADBEEF);
  buf.writeBe<uint64_t>(0x0123456789ABCDEFULL);

  expectDecodeValue(buf.readBe<uint16_t>(), 25565, "uint16_t port");
  expectDecodeValue(buf.readBe<uint32_t>(), 0xDEADBEEFu, "uint32_t");
  expectDecodeValue(buf.readBe<uint64_t>(), 0x0123456789ABCDEFULL, "uint64_t");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

// ---------------------------------------------------------------------------
// Minecraft 协议 String 类型测试 (VarInt 长度前缀 + UTF-8)
// ---------------------------------------------------------------------------

TEST(ByteBufferStringTest, StringRoundTrip) {
  ByteBuffer buf;
  buf.appendString("Hello Minecraft");
  EXPECT_EQ(*buf.readString(), "Hello Minecraft");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferStringTest, EmptyString) {
  ByteBuffer buf;
  buf.appendString("");
  EXPECT_EQ(*buf.readString(), "");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferStringTest, StringLengthLimitEnforced) {
  ByteBuffer buf;
  buf.appendString("1234567890");

  // 限制最大字节数为 5，应判定为畸形数据 kMalformed
  auto res = buf.readString(5);
  expectDecodeError(res, DecodeError::kMalformed, "超长字符串");
}

TEST(ByteBufferStringTest, HalfPacketRollsBackCleanly) {
  ByteBuffer buf;
  // 手动构造：VarInt 长度为 5，但只给了 2 字节内容
  buf.appendVarInt(5);
  buf.append("AB");

  // 读取失败，返回 kIncomplete
  auto res = buf.readString();
  expectDecodeError(res, DecodeError::kIncomplete, "半包字符串");

  // 验证事务回滚：读游标必须完全回滚到 VarInt 之前
  EXPECT_EQ(buf.readableBytes(), 1u + 2u) << "Mark 必须回滚已读的 VarInt 游标";

  // 补齐剩余 3 字节
  buf.append("CDE");
  expectDecodeValue(buf.readString(), "ABCDE", "数据补齐后成功重试");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

// ---------------------------------------------------------------------------
// 内存池原生接入与克隆测试
// ---------------------------------------------------------------------------

TEST(ByteBufferPooledTest, DefaultConstructIsPooled) {
  ByteBuffer buf;
  EXPECT_TRUE(buf.isPooled());
  buf.append("Hello Pool");
  EXPECT_EQ(buf.readableBytes(), 10u);
  EXPECT_EQ(buf.toStringView(), "Hello Pool");
}

TEST(ByteBufferPooledTest, CloneCreatesExactIndependentCopy) {
  ByteBuffer buf;
  buf.append("Original Data");
  ByteBuffer copy = buf.clone();

  EXPECT_EQ(copy.readableBytes(), 13u);
  EXPECT_EQ(copy.toStringView(), "Original Data");

  // 修改原 buf，不影响 copy
  buf.append(" Modified");
  EXPECT_EQ(buf.toStringView(), "Original Data Modified");
  EXPECT_EQ(copy.toStringView(), "Original Data");
}

TEST(ByteBufferPooledTest, OverCapacityMigratesToHeap) {
  ByteBuffer buf;
  EXPECT_TRUE(buf.isPooled());

  // 写入 5000 字节，突破 4KB 池化块容量
  std::string huge(5000, 'K');
  buf.append(huge);

  EXPECT_FALSE(buf.isPooled());
  EXPECT_EQ(buf.readableBytes(), 5000u);
  EXPECT_EQ(buf.toStringView(), huge);
}

// ---------------------------------------------------------------------------
// 性能基准测试：房间 50 人高频广播（区块/移动包）收益对比
// ---------------------------------------------------------------------------

TEST(ByteBufferPerfTest, Broadcast50PlayersBenchmark) {
  constexpr int kPlayers = 50;          // 房间 50 个玩家
  constexpr int kRounds = 10000;        // 10,000 轮广播（共 50 万次报文投递）
  constexpr size_t kPacketSize = 1024;  // 1KB 报文（如区块变更或实体状态）

  std::string payload(kPacketSize, 'M');

  // ① 传统方案：每次广播对 50 个玩家分别执行 50 次堆内存深拷贝（malloc + memcpy）
  auto start_heap = std::chrono::steady_clock::now();
  {
    for (int r = 0; r < kRounds; ++r) {
      std::vector<std::vector<char>> client_queues(kPlayers);
      for (int p = 0; p < kPlayers; ++p) {
        client_queues[p].assign(payload.begin(), payload.end());
        asm volatile("" : : "r"(client_queues[p].data()) : "memory");
      }
    }
  }
  auto elapsed_heap = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_heap).count();

  // ② 重构方案 A：默认使用 ShardedBufferPool 分片内存池独立借还（50 次无锁借池 + memcpy）
  auto start_pooled = std::chrono::steady_clock::now();
  {
    for (int r = 0; r < kRounds; ++r) {
      std::vector<ByteBuffer> client_queues;
      client_queues.reserve(kPlayers);
      for (int p = 0; p < kPlayers; ++p) {
        ByteBuffer buf;
        buf.append(payload);
        asm volatile("" : : "r"(buf.peek()) : "memory");
        client_queues.push_back(std::move(buf));
      }
    }
  }
  auto elapsed_pooled = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_pooled).count();

  // ③ 重构方案 B：单次序列化 + 引用计数浅共享广播（std::shared_ptr<ByteBuffer>，0 次数据拷贝）
  auto start_shared = std::chrono::steady_clock::now();
  {
    for (int r = 0; r < kRounds; ++r) {
      auto shared_packet = std::make_shared<ByteBuffer>();
      shared_packet->append(payload);

      std::vector<std::shared_ptr<ByteBuffer>> client_queues;
      client_queues.reserve(kPlayers);
      for (int p = 0; p < kPlayers; ++p) {
        client_queues.push_back(shared_packet);
        asm volatile("" : : "r"(client_queues[p]->peek()) : "memory");
      }
    }
  }
  auto elapsed_shared = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_shared).count();

  std::cout << "\n================ [ PERF 基准实验: 房间 50 人广播性能对比 ] ================\n"
            << "  测试配置: 50 玩家 x " << kRounds << " 轮广播 (共 " << (kPlayers * kRounds) << " 次包分发), 单包 1KB\n"
            << "  ------------------------------------------------------------------------\n"
            << "  ① 传统方案 (50 次堆分配 + 深拷贝)     : " << elapsed_heap << " ms\n"
            << "  ② 重构方案 A (50 次分片池借还 + 独立包)  : " << elapsed_pooled << " ms ("
            << (double)elapsed_heap / std::max<long long>(elapsed_pooled, 1) << "x 加速)\n"
            << "  ③ 重构方案 B (单次序列化 + 引用共享广播)  : " << elapsed_shared << " ms ("
            << (double)elapsed_heap / std::max<long long>(elapsed_shared, 1) << "x 加速, 较独立池化再提升 "
            << (double)elapsed_pooled / std::max<long long>(elapsed_shared, 1) << "x)\n"
            << "========================================================================\n\n";

  EXPECT_GT(elapsed_heap, 0);
  EXPECT_GT(elapsed_pooled, 0);
  EXPECT_GT(elapsed_shared, 0);
}

// 性能基准测试 ②：多线程高并发环境下的 50 人广播（直击全局堆锁争用）
TEST(ByteBufferPerfTest, Broadcast50PlayersMultiThreadedBenchmark) {
  constexpr int kThreads = 8;
  constexpr int kPlayers = 50;
  constexpr int kRounds = 3000;
  constexpr size_t kPacketSize = 1024;
  std::string payload(kPacketSize, 'M');

  // ① 传统方案：8 线程并发广播，争抢系统堆锁
  auto start_heap = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&]() {
        for (int r = 0; r < kRounds; ++r) {
          std::vector<std::vector<char>> client_queues(kPlayers);
          for (int p = 0; p < kPlayers; ++p) {
            client_queues[p].assign(payload.begin(), payload.end());
            asm volatile("" : : "r"(client_queues[p].data()) : "memory");
          }
        }
      });
    }
    for (auto& w : workers) w.join();
  }
  auto elapsed_heap = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_heap).count();

  // ② 重构方案 A：8 线程并发，各自分片池借还 (ShardedBufferPool)
  auto start_pooled = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&]() {
        for (int r = 0; r < kRounds; ++r) {
          std::vector<ByteBuffer> client_queues;
          client_queues.reserve(kPlayers);
          for (int p = 0; p < kPlayers; ++p) {
            ByteBuffer buf;
            buf.append(payload);
            asm volatile("" : : "r"(buf.peek()) : "memory");
            client_queues.push_back(std::move(buf));
          }
        }
      });
    }
    for (auto& w : workers) w.join();
  }
  auto elapsed_pooled = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_pooled).count();

  // ③ 重构方案 B：8 线程并发，单次序列化 + 引用共享广播 (std::shared_ptr<ByteBuffer>)
  auto start_shared = std::chrono::steady_clock::now();
  {
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
      workers.emplace_back([&]() {
        for (int r = 0; r < kRounds; ++r) {
          auto shared_packet = std::make_shared<ByteBuffer>();
          shared_packet->append(payload);

          std::vector<std::shared_ptr<ByteBuffer>> client_queues;
          client_queues.reserve(kPlayers);
          for (int p = 0; p < kPlayers; ++p) {
            client_queues.push_back(shared_packet);
            asm volatile("" : : "r"(client_queues[p]->peek()) : "memory");
          }
        }
      });
    }
    for (auto& w : workers) w.join();
  }
  auto elapsed_shared = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - start_shared).count();

  std::cout << "\n================ [ PERF 基准实验 ②: 8 线程并发 50 人广播争用对比 ] ================\n"
            << "  测试配置: 8 并发工作线程 x 50 玩家 x " << kRounds << " 轮 (共 " << (kThreads * kPlayers * kRounds) << " 次包分发)\n"
            << "  --------------------------------------------------------------------------------\n"
            << "  ① 传统多线程方案 (50 次堆分配 + 深拷贝)     : " << elapsed_heap << " ms\n"
            << "  ② 重构方案 A (分片池借还 + 独立包)          : " << elapsed_pooled << " ms ("
            << (double)elapsed_heap / std::max<long long>(elapsed_pooled, 1) << "x 加速)\n"
            << "  ③ 重构方案 B (单次序列化 + 引用共享广播)      : " << elapsed_shared << " ms ("
            << (double)elapsed_heap / std::max<long long>(elapsed_shared, 1) << "x 加速, 较独立池化再提升 "
            << (double)elapsed_pooled / std::max<long long>(elapsed_shared, 1) << "x)\n"
            << "====================================================================================\n\n";

  EXPECT_GT(elapsed_heap, 0);
  EXPECT_GT(elapsed_pooled, 0);
  EXPECT_GT(elapsed_shared, 0);
}


