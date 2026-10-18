#include "protocol/BungeeCord.h"

#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <vector>

namespace mcpe::protocol {

namespace {

int hexDigit(char ch) noexcept {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return -1;
}

void skipWhitespace(std::string_view sv, size_t& pos) noexcept {
  while (pos < sv.size() &&
         (sv[pos] == ' ' || sv[pos] == '\t' || sv[pos] == '\n' || sv[pos] == '\r')) {
    ++pos;
  }
}

std::optional<std::string> parseJsonString(std::string_view sv, size_t& pos) {
  skipWhitespace(sv, pos);
  if (pos >= sv.size() || sv[pos] != '"') {
    return std::nullopt;
  }
  ++pos;  // 跳过起始双引号 '"'
  std::string result;
  while (pos < sv.size()) {
    char ch = sv[pos++];
    if (ch == '"') {
      return result;
    }
    if (ch == '\\') {
      if (pos >= sv.size()) {
        return std::nullopt;
      }
      char esc = sv[pos++];
      switch (esc) {
        case '"':
          result.push_back('"');
          break;
        case '\\':
          result.push_back('\\');
          break;
        case '/':
          result.push_back('/');
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u': {
          if (pos + 4 > sv.size()) {
            return std::nullopt;
          }
          pos += 4;  // 跳过 unicode 码点
          break;
        }
        default:
          result.push_back(esc);
          break;
      }
    } else {
      result.push_back(ch);
    }
  }
  return std::nullopt;
}

}  // namespace

std::optional<BungeeHandshakeData>
BungeeCord::parseHandshakeAddress(std::string_view raw_address) {
  std::vector<std::string_view> parts;
  size_t start = 0;
  while (start <= raw_address.size()) {
    const size_t end = raw_address.find('\0', start);
    if (end == std::string_view::npos) {
      parts.push_back(raw_address.substr(start));
      break;
    }
    parts.push_back(raw_address.substr(start, end - start));
    start = end + 1;
  }

  // BungeeCord IP Forwarding 必须至少包含 4 段: host, ip, uuid, skin_json
  if (parts.size() < 4) {
    return std::nullopt;
  }

  BungeeHandshakeData data;
  data.virtual_host = std::string(parts[0]);
  data.real_ip = std::string(parts[1]);

  const std::string_view raw_uuid = parts[2];
  auto bytes_opt = parseUuidBytes(raw_uuid);
  if (!bytes_opt) {
    return std::nullopt;
  }
  data.uuid_bytes = *bytes_opt;
  data.uuid_str = formatStandardUuid(raw_uuid);

  data.skin_json = std::string(parts[3]);

  // 若存在第 5 段，则提取为 Bungee-Guard 令牌
  if (parts.size() >= 5) {
    data.guard_token = std::string(parts[4]);
  }

  // 解析 skin_json 属性数组，并尝试提取 JSON 内部的 bungeeguard-token
  data.properties = parsePropertiesJson(data.skin_json, &data.guard_token);

  return data;
}

std::string
BungeeCord::formatStandardUuid(std::string_view raw_uuid) {
  // 提取纯十六进制字符
  std::string hex_only;
  hex_only.reserve(32);
  for (char ch : raw_uuid) {
    if (ch == '-') continue;
    hex_only.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }

  if (hex_only.size() != 32) {
    return std::string(raw_uuid);
  }

  std::string res;
  res.reserve(36);
  res.append(hex_only.substr(0, 8));
  res.push_back('-');
  res.append(hex_only.substr(8, 4));
  res.push_back('-');
  res.append(hex_only.substr(12, 4));
  res.push_back('-');
  res.append(hex_only.substr(16, 4));
  res.push_back('-');
  res.append(hex_only.substr(20, 12));
  return res;
}

std::optional<std::array<uint8_t, 16>>
BungeeCord::parseUuidBytes(std::string_view raw_uuid) {
  std::array<uint8_t, 16> bytes{};
  size_t byte_idx = 0;
  int high_nibble = -1;

  for (char ch : raw_uuid) {
    if (ch == '-') continue;
    int val = hexDigit(ch);
    if (val < 0) {
      return std::nullopt;
    }
    if (high_nibble < 0) {
      high_nibble = val;
    } else {
      if (byte_idx < 16) {
        bytes[byte_idx++] = static_cast<uint8_t>((high_nibble << 4) | val);
      } else {
        return std::nullopt;  // 超过 16 字节
      }
      high_nibble = -1;
    }
  }

  if (byte_idx != 16 || high_nibble != -1) {
    return std::nullopt;  // 字节数不足 16 字节或存在孤立半字节
  }

  return bytes;
}

std::vector<PlayerProperty>
BungeeCord::parsePropertiesJson(std::string_view json, std::string* out_guard_token) {
  std::vector<PlayerProperty> properties;
  size_t pos = 0;
  skipWhitespace(json, pos);

  if (pos >= json.size() || json[pos] != '[') {
    return properties;
  }
  ++pos;  // 跳过 '['

  while (pos < json.size()) {
    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ']') {
      break;
    }

    if (pos < json.size() && json[pos] == '{') {
      ++pos;  // 跳过 '{'
      std::string name;
      std::string value;
      std::string signature;

      while (pos < json.size()) {
        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == '}') {
          ++pos;
          break;
        }

        auto key_opt = parseJsonString(json, pos);
        if (!key_opt) {
          break;
        }

        skipWhitespace(json, pos);
        if (pos >= json.size() || json[pos] != ':') {
          break;
        }
        ++pos;  // 跳过 ':'

        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == '"') {
          auto val_opt = parseJsonString(json, pos);
          if (!val_opt) {
            break;
          }
          if (*key_opt == "name") {
            name = std::move(*val_opt);
          } else if (*key_opt == "value") {
            value = std::move(*val_opt);
          } else if (*key_opt == "signature") {
            signature = std::move(*val_opt);
          }
        } else {
          // 跳过非字符串原始字面量 (true, false, null, 数字等)
          while (pos < json.size() && json[pos] != ',' && json[pos] != '}') {
            ++pos;
          }
        }

        skipWhitespace(json, pos);
        if (pos < json.size() && json[pos] == ',') {
          ++pos;
        }
      }

      if (name == "bungeeguard-token") {
        if (out_guard_token && out_guard_token->empty()) {
          *out_guard_token = std::move(value);
        }
      } else if (!name.empty()) {
        properties.push_back(PlayerProperty{
            .name = std::move(name),
            .value = std::move(value),
            .signature = std::move(signature)
        });
      }
    }

    skipWhitespace(json, pos);
    if (pos < json.size() && json[pos] == ',') {
      ++pos;
    } else if (pos < json.size() && json[pos] == ']') {
      break;
    } else {
      ++pos;
    }
  }

  return properties;
}

}  // namespace mcpe::protocol
