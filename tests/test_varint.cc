// TASK-0.5：VarInt / VarLong 边界极限与错误处理单元测试
//
// 权威依据（本地存档）：
//   doc/reference/wiki.vg/Data_types.txt  第 116~242 行
//     · 116~174 行 —— 读写伪代码（Java 参考实现）
//     · 175~205 行 —— 官方 Sample VarInts 向量表
//     · 208~241 行 —— 官方 Sample VarLongs 向量表
//
// 错误契约（所有解析函数统一遵守，见 ByteBuffer.h 的 DecodeError 注释）：
//   成功     -> std::expected 携带值
//   kIncomplete 数据不足（TCP 半包）—— 正常控制流，等下一次 read
//   kMalformed  畸形 / 超上限 —— 不可信输入，断连
//   **失败零副作用**：失败时不消费任何字节，且不可能取到值
//   （取值请用 has_value() / operator* / value_or，**不要用 .value()**：它在错误时会抛异常）

#include <gtest/gtest.h>

#include <cstdint>
#include <expected>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "mcpe/common/ByteBuffer.h"

using mcpe::ByteBuffer;
using mcpe::DecodeError;

// 让 gtest 的失败信息直接打印 kIncomplete / kMalformed，而不是数字
void PrintTo(DecodeError e, std::ostream* os) { *os << mcpe::toString(e); }

namespace {

// ---------------------------------------------------------------------------
// 辅助
// ---------------------------------------------------------------------------

// 把可读区域渲染成 "80 01"，失败信息一眼可读
std::string hexOf(const ByteBuffer& buf) {
  static const char* kDigits = "0123456789abcdef";
  std::string out;
  for (unsigned char c : buf.toStringView()) {
    if (!out.empty()) out.push_back(' ');
    out.push_back(kDigits[c >> 4]);
    out.push_back(kDigits[c & 0x0F]);
  }
  return out;
}

// 把 "80 01" 形式的期望值解析成真实字节
std::vector<uint8_t> parseHex(std::string_view text) {
  std::vector<uint8_t> raw;
  for (size_t i = 0; i < text.size();) {
    while (i < text.size() && text[i] == ' ') ++i;
    if (i + 1 >= text.size()) break;
    raw.push_back(static_cast<uint8_t>(std::stoul(std::string(text.substr(i, 2)), nullptr, 16)));
    i += 2;
  }
  return raw;
}

ByteBuffer bufferOf(std::initializer_list<uint8_t> bytes) {
  ByteBuffer buf;
  for (uint8_t b : bytes) buf.appendByte(b);
  return buf;
}

ByteBuffer bufferOfHex(std::string_view hex) {
  ByteBuffer buf;
  const std::vector<uint8_t> raw = parseHex(hex);
  buf.append(raw.data(), raw.size());
  return buf;
}

// 解析成功：返回值必须相等（第二个模板参数允许直接传整型字面量）
template <typename T, typename U>
void expectOk(const std::expected<T, DecodeError>& result, U expected, const char* what) {
  if (!result.has_value()) {
    ADD_FAILURE() << what << " 期望成功，实际失败: " << mcpe::toString(result.error());
    return;
  }
  EXPECT_EQ(*result, static_cast<T>(expected)) << what;
}

// 解析失败：错误分类必须精确匹配（kIncomplete 与 kMalformed 不可混）
template <typename T>
void expectErr(const std::expected<T, DecodeError>& result, DecodeError expected,
               const char* what) {
  if (result.has_value()) {
    ADD_FAILURE() << what << " 期望失败，却解析成功了: " << *result;
    return;
  }
  EXPECT_EQ(result.error(), expected) << what;
}

// 断言编码结果与官方向量逐字节一致
void expectVarIntEncoding(int32_t value, std::string_view expected_hex) {
  ByteBuffer buf;
  buf.appendVarInt(value);
  EXPECT_EQ(hexOf(buf), expected_hex) << "appendVarInt(" << value << ")";
  EXPECT_LE(buf.readableBytes(), ByteBuffer::kMaxVarIntBytes)
      << "appendVarInt(" << value << ") 超过 5 字节";
}

void expectVarLongEncoding(int64_t value, std::string_view expected_hex) {
  ByteBuffer buf;
  buf.appendVarLong(value);
  EXPECT_EQ(hexOf(buf), expected_hex) << "appendVarLong(" << value << ")";
  EXPECT_LE(buf.readableBytes(), ByteBuffer::kMaxVarLongBytes)
      << "appendVarLong(" << value << ") 超过 10 字节";
}

// 断言解码结果与官方向量一致，且"恰好消费完"
void expectVarIntDecoding(std::string_view hex, int32_t expected) {
  ByteBuffer buf = bufferOfHex(hex);
  expectOk(buf.readVarInt(), expected, "readVarInt");
  EXPECT_EQ(buf.readableBytes(), 0u) << "解码后应恰好消费完 " << hex;
}

void expectVarLongDecoding(std::string_view hex, int64_t expected) {
  ByteBuffer buf = bufferOfHex(hex);
  expectOk(buf.readVarLong(), expected, "readVarLong");
  EXPECT_EQ(buf.readableBytes(), 0u) << "解码后应恰好消费完 " << hex;
}

}  // namespace

// ---------------------------------------------------------------------------
// 官方 Sample VarInts（Data_types.txt 第 175~205 行）
// ---------------------------------------------------------------------------
struct VarIntVector {
  const char* name;
  int32_t value;
  const char* hex;
};

class OfficialVarIntVector : public ::testing::TestWithParam<VarIntVector> {};

TEST_P(OfficialVarIntVector, Encode) {
  const auto& v = GetParam();
  expectVarIntEncoding(v.value, v.hex);
}

TEST_P(OfficialVarIntVector, Decode) {
  const auto& v = GetParam();
  expectVarIntDecoding(v.hex, v.value);
}

INSTANTIATE_TEST_SUITE_P(
    Official, OfficialVarIntVector,
    ::testing::Values(VarIntVector{"zero", 0, "00"}, VarIntVector{"one", 1, "01"},
                      VarIntVector{"two", 2, "02"}, VarIntVector{"127", 127, "7f"},
                      VarIntVector{"128", 128, "80 01"}, VarIntVector{"255", 255, "ff 01"},
                      VarIntVector{"int32_max", 2147483647, "ff ff ff ff 07"},
                      VarIntVector{"minus_one", -1, "ff ff ff ff 0f"},
                      VarIntVector{"int32_min", std::numeric_limits<int32_t>::min(),
                                   "80 80 80 80 08"}),
    [](const ::testing::TestParamInfo<VarIntVector>& info) {
      return std::string(info.param.name);
    });

// ---------------------------------------------------------------------------
// 官方 Sample VarLongs（Data_types.txt 第 208~241 行）
// ---------------------------------------------------------------------------
struct VarLongVector {
  const char* name;
  int64_t value;
  const char* hex;
};

class OfficialVarLongVector : public ::testing::TestWithParam<VarLongVector> {};

TEST_P(OfficialVarLongVector, Encode) {
  const auto& v = GetParam();
  expectVarLongEncoding(v.value, v.hex);
}

TEST_P(OfficialVarLongVector, Decode) {
  const auto& v = GetParam();
  expectVarLongDecoding(v.hex, v.value);
}

INSTANTIATE_TEST_SUITE_P(
    Official, OfficialVarLongVector,
    ::testing::Values(VarLongVector{"zero", 0, "00"}, VarLongVector{"one", 1, "01"},
                      VarLongVector{"two", 2, "02"}, VarLongVector{"127", 127, "7f"},
                      VarLongVector{"128", 128, "80 01"}, VarLongVector{"255", 255, "ff 01"},
                      VarLongVector{"int32_max", 2147483647, "ff ff ff ff 07"},
                      VarLongVector{"int64_max", std::numeric_limits<int64_t>::max(),
                                    "ff ff ff ff ff ff ff ff 7f"},
                      VarLongVector{"minus_one", -1, "ff ff ff ff ff ff ff ff ff 01"},
                      VarLongVector{"int32_min_as_varlong", -2147483648LL,
                                    "80 80 80 80 f8 ff ff ff ff 01"},
                      VarLongVector{"int64_min", std::numeric_limits<int64_t>::min(),
                                    "80 80 80 80 80 80 80 80 80 01"}),
    [](const ::testing::TestParamInfo<VarLongVector>& info) {
      return std::string(info.param.name);
    });

// ---------------------------------------------------------------------------
// 零值：协议明文规定 "0 -> 0x00"，绝不能编成空字节流
// ---------------------------------------------------------------------------
TEST(VarIntTest, ZeroEncodesToExactlyOneByte) {
  ByteBuffer vi;
  vi.appendVarInt(0);
  EXPECT_EQ(vi.readableBytes(), 1u) << "VarInt(0) 必须是 1 字节 0x00，不能是空";
  EXPECT_EQ(hexOf(vi), std::string("00"));

  ByteBuffer vl;
  vl.appendVarLong(0);
  EXPECT_EQ(vl.readableBytes(), 1u) << "VarLong(0) 必须是 1 字节 0x00";
  EXPECT_EQ(hexOf(vl), std::string("00"));
}

// ---------------------------------------------------------------------------
// 各长度档位的边界值（2^7 / 2^14 / 2^21 / 2^28 是每一档的起点，差一即换档）
// ---------------------------------------------------------------------------
TEST(VarIntTest, ByteLengthClassBoundaries) {
  const struct {
    int32_t value;
    size_t bytes;
  } cases[] = {
      {0, 1},         {127, 1},
      {128, 2},       {16383, 2},
      {16384, 3},     {2097151, 3},
      {2097152, 4},   {268435455, 4},
      {268435456, 5}, {std::numeric_limits<int32_t>::max(), 5},
      {-1, 5},        {std::numeric_limits<int32_t>::min(), 5},
  };
  for (const auto& c : cases) {
    ByteBuffer buf;
    buf.appendVarInt(c.value);
    EXPECT_EQ(buf.readableBytes(), c.bytes) << "appendVarInt(" << c.value << ")";
  }
}

TEST(VarLongTest, ByteLengthClassBoundaries) {
  const struct {
    int64_t value;
    size_t bytes;
  } cases[] = {
      {0, 1},
      {127, 1},
      {128, 2},
      {std::numeric_limits<int32_t>::max(), 5},
      {2147483648LL, 5},
      {34359738367LL, 5},
      {34359738368LL, 6},
      {std::numeric_limits<int64_t>::max(), 9},
      {-1, 10},
      {std::numeric_limits<int64_t>::min(), 10},
  };
  for (const auto& c : cases) {
    ByteBuffer buf;
    buf.appendVarLong(c.value);
    EXPECT_EQ(buf.readableBytes(), c.bytes) << "appendVarLong(" << c.value << ")";
  }
}

// ---------------------------------------------------------------------------
// 编解码往返一致性（覆盖所有长度档位与负数）
// ---------------------------------------------------------------------------
TEST(VarIntTest, RoundTrip) {
  const int32_t values[] = {
      0,         1,
      2,         127,
      128,       255,
      256,       16383,
      16384,     2097151,
      2097152,   268435455,
      268435456, std::numeric_limits<int32_t>::max(),
      -1,        -2,
      -127,      -128,
      -129,      -16384,
      -2097152,  std::numeric_limits<int32_t>::min(),
  };
  for (int32_t v : values) {
    ByteBuffer buf;
    buf.appendVarInt(v);
    expectOk(buf.readVarInt(), v, "round-trip");
    EXPECT_EQ(buf.readableBytes(), 0u) << "round-trip 后有残留, value=" << v;
  }
}

TEST(VarLongTest, RoundTrip) {
  const int64_t values[] = {
      0,
      1,
      127,
      128,
      std::numeric_limits<int32_t>::max(),
      2147483648LL,
      -2147483649LL,
      std::numeric_limits<int64_t>::max(),
      -1,
      std::numeric_limits<int64_t>::min(),
  };
  for (int64_t v : values) {
    ByteBuffer buf;
    buf.appendVarLong(v);
    expectOk(buf.readVarLong(), v, "round-trip");
    EXPECT_EQ(buf.readableBytes(), 0u) << "round-trip 后有残留, value=" << v;
  }
}

// ---------------------------------------------------------------------------
// 同一数值在 VarInt / VarLong 下的编码长度必须不同（防止两个实现写混）
// ---------------------------------------------------------------------------
TEST(VarIntTest, VarIntAndVarLongDifferForSameValue) {
  const int64_t value = -2147483648LL;

  ByteBuffer vi;
  vi.appendVarInt(static_cast<int32_t>(value));
  EXPECT_EQ(hexOf(vi), std::string("80 80 80 80 08"));

  ByteBuffer vl;
  vl.appendVarLong(value);
  EXPECT_EQ(hexOf(vl), std::string("80 80 80 80 f8 ff ff ff ff 01"));
}

// ---------------------------------------------------------------------------
// 消费行为：解码必须不多吃也不少吃到后面的字节（粘包场景的前提）
// ---------------------------------------------------------------------------
TEST(VarIntTest, DecodeConsumesExactlyEncodedBytes) {
  ByteBuffer buf;
  buf.appendVarInt(300);  // 0xac 0x02
  buf.appendByte(0xAB);   // 哨兵
  buf.appendByte(0xCD);

  expectOk(buf.readVarInt(), 300, "readVarInt(300)");
  EXPECT_EQ(buf.readableBytes(), 2u) << "解码消费了多余的字节";
  EXPECT_EQ(buf.readBe<uint8_t>(), 0xAB);
  EXPECT_EQ(buf.readBe<uint8_t>(), 0xCD);
}

TEST(VarLongTest, DecodeConsumesExactlyEncodedBytes) {
  ByteBuffer buf;
  buf.appendVarLong(-1);  // 10 字节
  buf.appendByte(0x5A);

  expectOk(buf.readVarLong(), -1, "readVarLong(-1)");
  EXPECT_EQ(buf.readableBytes(), 1u);
  EXPECT_EQ(buf.readBe<uint8_t>(), 0x5A);
}

// ---------------------------------------------------------------------------
// 与定长大端整数区分：VarInt 是「7 位小端分组」，不是大端
// ---------------------------------------------------------------------------
TEST(VarIntTest, IsNotBigEndianInt32) {
  ByteBuffer vi;
  vi.appendVarInt(128);
  EXPECT_EQ(hexOf(vi), std::string("80 01")) << "VarInt(128) 是 80 01 而不是 00 00 00 80";

  ByteBuffer be;
  be.writeBe<int32_t>(128);
  EXPECT_EQ(hexOf(be), std::string("00 00 00 80")) << "writeBe<int32_t> 才是大端";
}

// ---------------------------------------------------------------------------
// 连续解码（Packet Length + Packet ID 这种一个包内多字段场景）
// ---------------------------------------------------------------------------
TEST(VarIntTest, MultipleVarIntsInOneBuffer) {
  ByteBuffer buf;
  buf.appendVarInt(5);     // Packet Length
  buf.appendVarInt(0x23);  // Packet ID = Join Game
  buf.appendVarInt(-1);    // 负数负载

  expectOk(buf.readVarInt(), 5, "Packet Length");
  expectOk(buf.readVarInt(), 0x23, "Packet ID");
  expectOk(buf.readVarInt(), -1, "payload");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

// ===========================================================================
// 错误处理：kIncomplete（半包）
// ===========================================================================

TEST(VarIntErrorTest, EmptyBufferIsIncomplete) {
  ByteBuffer buf;
  expectErr(buf.readVarInt(), DecodeError::kIncomplete, "空缓冲区");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(VarIntErrorTest, TrailingContinuationBitIsIncomplete) {
  // 0x80 表示"后面还有字节"，所以 {0x80} 只是半个 VarInt
  ByteBuffer buf = bufferOf({0x80});
  expectErr(buf.readVarInt(), DecodeError::kIncomplete, "半个 VarInt");
  EXPECT_EQ(buf.readableBytes(), 1u) << "kIncomplete 不得消费任何字节";

  ByteBuffer buf2 = bufferOf({0x80, 0x80, 0x80, 0x80});
  expectErr(buf2.readVarInt(), DecodeError::kIncomplete, "4 字节都是继续位");
  EXPECT_EQ(buf2.readableBytes(), 4u);
}

// 最关键的一条：模拟 TCP 半包 —— 数据分批到达，游标必须能"原地重试"
TEST(VarIntErrorTest, HalfPacketThenRemainderArrives) {
  ByteBuffer buf;
  buf.appendByte(0x80);  // VarInt(128) 的前半截

  expectErr(buf.readVarInt(), DecodeError::kIncomplete, "半包");
  EXPECT_EQ(buf.readableBytes(), 1u) << "半包时不得消费";

  buf.appendByte(0x01);  // 剩余字节到了
  expectOk(buf.readVarInt(), 128, "补齐后原地重试");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

// 半包时不消费 → 与后续字段的边界完全不错位
TEST(VarIntErrorTest, HalfPacketDoesNotDisturbFollowingField) {
  ByteBuffer buf;
  buf.appendVarInt(300);  // 完整字段
  buf.appendByte(0x80);   // 第二个字段的半截

  expectOk(buf.readVarInt(), 300, "第一个字段");
  expectErr(buf.readVarInt(), DecodeError::kIncomplete, "第二个字段");
  EXPECT_EQ(buf.readableBytes(), 1u) << "游标应停在半截字段的起点";
}

// ===========================================================================
// 错误处理：kMalformed（畸形 / 超上限）
// ===========================================================================

TEST(VarIntErrorTest, SixByteVarIntIsMalformed) {
  // 5 字节全是继续位 → 必然需要第 6 字节 → 突破协议上限
  ByteBuffer buf = bufferOf({0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  expectErr(buf.readVarInt(), DecodeError::kMalformed, "5 字节继续位");
  EXPECT_EQ(buf.readableBytes(), 5u) << "kMalformed 也不得消费字节";
}

TEST(VarIntErrorTest, MalformedIsNotMaskedByMoreData) {
  // 后面再跟多少数据都不影响判定：第 5 字节仍带继续位就已经违规
  ByteBuffer buf = bufferOf({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01, 0x02, 0x03});
  expectErr(buf.readVarInt(), DecodeError::kMalformed, "畸形 + 后续数据");
  EXPECT_EQ(buf.readableBytes(), 8u);
}

TEST(VarLongErrorTest, ElevenByteVarLongIsMalformed) {
  ByteBuffer buf = bufferOf({0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  expectErr(buf.readVarLong(), DecodeError::kMalformed, "10 字节继续位");
  EXPECT_EQ(buf.readableBytes(), 10u);
}

// 合法最大长度不能被误判为畸形
TEST(VarIntErrorTest, LegalMaximumLengthIsOk) {
  expectVarIntDecoding("ff ff ff ff 0f", -1);                  // VarInt  -1：5 字节
  expectVarLongDecoding("ff ff ff ff ff ff ff ff ff 01", -1);  // VarLong -1：10 字节
}

// 4 字节继续位 = 半包（可能还差第 5 字节）；5 字节继续位 = 畸形。两者不能混。
TEST(VarIntErrorTest, IncompleteAndMalformedAreDistinguished) {
  ByteBuffer four = bufferOf({0xFF, 0xFF, 0xFF, 0xFF});
  expectErr(four.readVarInt(), DecodeError::kIncomplete, "4 字节继续位");

  ByteBuffer five = bufferOf({0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  expectErr(five.readVarInt(), DecodeError::kMalformed, "5 字节继续位");
}

// ===========================================================================
// 错误处理：peek（非消费）
// ===========================================================================

TEST(VarIntErrorTest, PeekNeverConsumes) {
  ByteBuffer buf = bufferOf({0x80, 0x01, 0xAA});

  expectOk(buf.peekVarInt(), 128, "peek");
  EXPECT_EQ(buf.readableBytes(), 3u) << "peek 不得消费";

  // 可以反复 peek，结果稳定
  expectOk(buf.peekVarInt(), 128, "再次 peek");
  EXPECT_EQ(buf.readableBytes(), 3u);

  // 再真正读走，剩下的哨兵字节仍在
  expectOk(buf.readVarInt(), 128, "tryRead");
  EXPECT_EQ(buf.readableBytes(), 1u);
  EXPECT_EQ(buf.readBe<uint8_t>(), 0xAA);
}

TEST(VarIntErrorTest, PeekReportsErrorsWithoutConsuming) {
  ByteBuffer buf = bufferOf({0x80});
  expectErr(buf.peekVarInt(), DecodeError::kIncomplete, "peek 半包");
  EXPECT_EQ(buf.readableBytes(), 1u);
}

// ===========================================================================
// 错误处理：Mark（RAII 读游标事务）
// ===========================================================================

TEST(VarIntErrorTest, MarkRollsBackOnPartialFailure) {
  ByteBuffer buf;
  buf.appendVarInt(7);   // 第一个字段正常
  buf.appendByte(0x80);  // 第二个字段只有半截

  {
    ByteBuffer::Mark mark(buf);
    expectOk(buf.readVarInt(), 7, "第一个字段");
    expectErr(buf.readVarInt(), DecodeError::kIncomplete, "第二个字段");
    // 不 commit：析构自动回滚
  }
  EXPECT_EQ(buf.readableBytes(), 2u) << "整个包体应被完整回滚，一个字节都不消费";
  EXPECT_EQ(hexOf(buf), std::string("07 80"));

  // 回滚后仍能从第一个字段重新解析
  expectOk(buf.readVarInt(), 7, "回滚后重新解析");
}

TEST(VarIntErrorTest, MarkCommitKeepsConsumedBytes) {
  ByteBuffer buf;
  buf.appendVarInt(300);
  buf.appendByte(0x5A);

  {
    ByteBuffer::Mark mark(buf);
    expectOk(buf.readVarInt(), 300, "字段");
    mark.commit();
  }
  EXPECT_EQ(buf.readableBytes(), 1u) << "commit 之后不得回滚";
  EXPECT_EQ(buf.readBe<uint8_t>(), 0x5A);
}

TEST(VarIntErrorTest, MarkRollbackIsIdempotent) {
  ByteBuffer buf;
  buf.appendVarInt(1);
  buf.appendVarInt(2);

  {
    ByteBuffer::Mark mark(buf);
    expectOk(buf.readVarInt(), 1, "字段");
    mark.rollback();
    mark.rollback();  // 重复回滚必须安全
  }
  EXPECT_EQ(buf.readableBytes(), 2u);
}

// ===========================================================================
// 宽容性：非最短编码（协议未禁止）不应被当成畸形
// ===========================================================================

TEST(VarIntErrorTest, NonMinimalEncodingIsAccepted) {
  ByteBuffer buf = bufferOf({0x80, 0x00});  // 数值 0，但用了 2 字节
  expectOk(buf.readVarInt(), 0, "非最短编码");
  EXPECT_EQ(buf.readableBytes(), 0u);
}

// ===========================================================================
// std::expected 的结构性保证：失败时"根本没有值可拿"
// （这正是选它而不是 bool + 出参的原因 —— 不存在未初始化/陈旧的 out 参数被误用）
// ===========================================================================

TEST(VarIntErrorTest, FailedParseCarriesNoValue) {
  ByteBuffer buf = bufferOf({0x80});
  auto r = buf.readVarInt();
  ASSERT_FALSE(r.has_value());
  // value_or 是安全取值方式；注意**不要**用 r.value()，它在错误时会抛异常
  EXPECT_EQ(r.value_or(-42), -42) << "失败结果里不应携带任何可用值";
}

// ===========================================================================
// 解码极值测试：边界极值往返验证
// ===========================================================================

TEST(VarIntTest, ExtremeValuesRoundTrip) {
  ByteBuffer buf;
  buf.appendVarInt(std::numeric_limits<int32_t>::min());
  buf.appendVarLong(std::numeric_limits<int64_t>::min());

  EXPECT_EQ(*buf.readVarInt(), std::numeric_limits<int32_t>::min());
  EXPECT_EQ(*buf.readVarLong(), std::numeric_limits<int64_t>::min());
  EXPECT_EQ(buf.readableBytes(), 0u);
}

// ===========================================================================
// 独立 VarInt 模块与带字节计数 API 测试
// ===========================================================================

TEST(StandaloneVarIntTest, EncodeAndDecodeSpan) {
  const auto encoded = mcpe::varint::encodeVarInt(300);
  EXPECT_EQ(encoded.size, 2u);
  // 300 in VarInt is 0xAC 0x02
  EXPECT_EQ(encoded.data[0], static_cast<char>(0xAC));
  EXPECT_EQ(encoded.data[1], static_cast<char>(0x02));

  // decode over std::span
  const auto decoded = mcpe::varint::decodeVarInt(encoded.span());
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->value, 300);
  EXPECT_EQ(decoded->bytes, 2u);
}

TEST(StandaloneVarIntTest, VarLongEncodeAndDecode) {
  const int64_t val = 123456789012345LL;
  const auto encoded = mcpe::varint::encodeVarLong(val);
  const auto decoded = mcpe::varint::decodeVarLong(encoded.span());
  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->value, val);
  EXPECT_EQ(decoded->bytes, encoded.size);
}

TEST(ByteBufferWithBytesTest, PeekAndReadVarIntWithBytes) {
  ByteBuffer buf;
  buf.appendVarInt(25565);  // 常见端口 25565 > 16384，在 VarInt 中占用 3 字节 (DD C7 01)

  // 1. peek 不消费
  const auto peek_res = buf.peekVarIntWithBytes();
  ASSERT_TRUE(peek_res.has_value());
  EXPECT_EQ(peek_res->value, 25565);
  EXPECT_EQ(peek_res->bytes, 3u);
  EXPECT_EQ(buf.readableBytes(), 3u);

  // 2. read 消费
  const auto read_res = buf.readVarIntWithBytes();
  ASSERT_TRUE(read_res.has_value());
  EXPECT_EQ(read_res->value, 25565);
  EXPECT_EQ(read_res->bytes, 3u);
  EXPECT_EQ(buf.readableBytes(), 0u);
}

TEST(ByteBufferWithBytesTest, PrependVarInt) {
  ByteBuffer buf;
  buf.append("DATA", 4);
  // 前插 VarInt 长度 4 (1 字节)
  buf.prependVarInt(4);
  EXPECT_EQ(buf.readableBytes(), 5u);

  const auto peek_res = buf.peekVarIntWithBytes();
  ASSERT_TRUE(peek_res.has_value());
  EXPECT_EQ(peek_res->value, 4);
  EXPECT_EQ(peek_res->bytes, 1u);

  const auto len = buf.readVarInt();
  ASSERT_TRUE(len.has_value());
  EXPECT_EQ(*len, 4);
  EXPECT_EQ(buf.retrieveAllAsString(), "DATA");
}

