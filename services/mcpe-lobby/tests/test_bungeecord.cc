/**
 * @file test_bungeecord.cc
 * @brief BungeeCord / Velocity IP Forwarding 穿透解析与防伪测试
 */

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>

#include "protocol/BungeeCord.h"

using namespace mcpe::protocol;

TEST(BungeeCordTest, FormatStandardUuid) {
  // 32 位无连字符十六进制 UUID 转换为 36 字符标准格式
  EXPECT_EQ(
      BungeeCord::formatStandardUuid("5627dd98e6be3c21b8a8e92344183641"),
      "5627dd98-e6be-3c21-b8a8-e92344183641"
  );
  EXPECT_EQ(
      BungeeCord::formatStandardUuid("0123456789ABCDEF0123456789ABCDEF"),
      "01234567-89ab-cdef-0123-456789abcdef"
  );

  // 已经带有连字符的 36 字符 UUID 保持不变并转为小写
  EXPECT_EQ(
      BungeeCord::formatStandardUuid("5627dd98-e6be-3c21-b8a8-e92344183641"),
      "5627dd98-e6be-3c21-b8a8-e92344183641"
  );
}

TEST(BungeeCordTest, ParseUuidBytes) {
  auto bytes_opt = BungeeCord::parseUuidBytes("5627dd98e6be3c21b8a8e92344183641");
  ASSERT_TRUE(bytes_opt.has_value());
  const std::array<uint8_t, 16> expected{
      0x56, 0x27, 0xdd, 0x98, 0xe6, 0xbe, 0x3c, 0x21,
      0xb8, 0xa8, 0xe9, 0x23, 0x44, 0x18, 0x36, 0x41
  };
  EXPECT_EQ(*bytes_opt, expected);

  // 带连字符的 36 字符格式
  auto bytes_opt2 = BungeeCord::parseUuidBytes("5627dd98-e6be-3c21-b8a8-e92344183641");
  ASSERT_TRUE(bytes_opt2.has_value());
  EXPECT_EQ(*bytes_opt2, expected);

  // 非法字符与非法长度
  EXPECT_FALSE(BungeeCord::parseUuidBytes("invalid-uuid-string-here").has_value());
  EXPECT_FALSE(BungeeCord::parseUuidBytes("5627dd98e6be3c21").has_value());
  EXPECT_FALSE(BungeeCord::parseUuidBytes("5627dd98e6be3c21b8a8e92344183641ff").has_value());
}

TEST(BungeeCordTest, PropertiesJsonParsing) {
  // 1. 空或非法 JSON
  EXPECT_TRUE(BungeeCord::parsePropertiesJson("").empty());
  EXPECT_TRUE(BungeeCord::parsePropertiesJson("[]").empty());

  // 2. 标准单属性（包含签名）
  const std::string json =
      R"([{"name":"textures","value":"eyJ0ZXh0dXJlcyI6e319","signature":"c2lnbmF0dXJl"}])";
  auto props = BungeeCord::parsePropertiesJson(json);
  ASSERT_EQ(props.size(), 1u);
  EXPECT_EQ(props[0].name, "textures");
  EXPECT_EQ(props[0].value, "eyJ0ZXh0dXJlcyI6e319");
  EXPECT_EQ(props[0].signature, "c2lnbmF0dXJl");

  // 3. 包含 BungeeGuard 令牌的复合属性
  const std::string json_with_guard =
      R"([{"name":"textures","value":"base64_data","signature":"sig_data"},{"name":"bungeeguard-token","value":"secret-token-xyz"}])";
  std::string guard_token;
  auto props_guard = BungeeCord::parsePropertiesJson(json_with_guard, &guard_token);
  EXPECT_EQ(guard_token, "secret-token-xyz");
  // bungeeguard-token 必须从客户端属性列表中安全剥离
  ASSERT_EQ(props_guard.size(), 1u);
  EXPECT_EQ(props_guard[0].name, "textures");
  EXPECT_EQ(props_guard[0].value, "base64_data");
}

TEST(BungeeCordTest, ParseStandard4PartHandshake) {
  std::string raw;
  raw.append("mc.example.com");
  raw.push_back('\0');
  raw.append("192.168.1.100:54321");
  raw.push_back('\0');
  raw.append("5627dd98e6be3c21b8a8e92344183641");
  raw.push_back('\0');
  raw.append(R"([{"name":"textures","value":"abc","signature":"xyz"}])");

  auto data_opt = BungeeCord::parseHandshakeAddress(raw);
  ASSERT_TRUE(data_opt.has_value());
  EXPECT_EQ(data_opt->virtual_host, "mc.example.com");
  EXPECT_EQ(data_opt->real_ip, "192.168.1.100:54321");
  EXPECT_EQ(data_opt->uuid_str, "5627dd98-e6be-3c21-b8a8-e92344183641");
  EXPECT_TRUE(data_opt->guard_token.empty());
  ASSERT_EQ(data_opt->properties.size(), 1u);
  EXPECT_EQ(data_opt->properties[0].name, "textures");
  EXPECT_EQ(data_opt->properties[0].value, "abc");
  EXPECT_EQ(data_opt->properties[0].signature, "xyz");
}

TEST(BungeeCordTest, Parse5PartHandshakeWithToken) {
  std::string raw;
  raw.append("play.hypixel.net");
  raw.push_back('\0');
  raw.append("1.2.3.4");
  raw.push_back('\0');
  raw.append("5627dd98e6be3c21b8a8e92344183641");
  raw.push_back('\0');
  raw.append("[]");
  raw.push_back('\0');
  raw.append("forwarding_secret_2026");

  auto data_opt = BungeeCord::parseHandshakeAddress(raw);
  ASSERT_TRUE(data_opt.has_value());
  EXPECT_EQ(data_opt->virtual_host, "play.hypixel.net");
  EXPECT_EQ(data_opt->real_ip, "1.2.3.4");
  EXPECT_EQ(data_opt->uuid_str, "5627dd98-e6be-3c21-b8a8-e92344183641");
  EXPECT_EQ(data_opt->guard_token, "forwarding_secret_2026");
  EXPECT_TRUE(data_opt->properties.empty());
}

TEST(BungeeCordTest, MalformedHandshakes) {
  // 少于 4 段的格式
  EXPECT_FALSE(BungeeCord::parseHandshakeAddress("mc.example.com").has_value());

  std::string raw2 = "host\0ip\0uuid";
  EXPECT_FALSE(BungeeCord::parseHandshakeAddress(raw2).has_value());

  // UUID 非法
  std::string raw_bad_uuid;
  raw_bad_uuid.append("host");
  raw_bad_uuid.push_back('\0');
  raw_bad_uuid.append("127.0.0.1");
  raw_bad_uuid.push_back('\0');
  raw_bad_uuid.append("not-a-valid-hex-uuid");
  raw_bad_uuid.push_back('\0');
  raw_bad_uuid.append("[]");
  EXPECT_FALSE(BungeeCord::parseHandshakeAddress(raw_bad_uuid).has_value());
}
