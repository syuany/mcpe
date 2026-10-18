#ifndef MCPE_PROTOCOL_BUNGEECORD_H
#define MCPE_PROTOCOL_BUNGEECORD_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "protocol/PacketTypes.h"

namespace mcpe::protocol {

/**
 * @brief BungeeCord / Velocity IP 穿透握手解析结果
 */
struct BungeeHandshakeData {
  std::string virtual_host;              // 原始连接主机名
  std::string real_ip;                   // 客户端真实 IP (可能带端口)
  std::string uuid_str;                  // 转换后的标准 RFC 4122 UUID (含 4 个连字符, 36 字符)
  std::array<uint8_t, 16> uuid_bytes{};  // 16 字节原始 UUID 二进制数组
  std::string skin_json;                 // Mojang 签名皮肤属性 JSON 字符串
  std::string guard_token;               // Bungee-Guard 密钥 (可选)
  std::vector<PlayerProperty> properties; // 解析出的有效玩家属性列表 (已安全剥离 guard_token)
};

class BungeeCord {
 public:
  /**
   * @brief 解析握手包中以 \0 分隔的 Server Address 字符串
   * @param raw_address 原始字符串 (形如 host\0ip\0uuid\0skin[\0token])
   * @return 解析成功返回 BungeeHandshakeData，若格式不符返回 std::nullopt
   */
  [[nodiscard]] static std::optional<BungeeHandshakeData> parseHandshakeAddress(
      std::string_view raw_address
  );

  /**
   * @brief 将 32 位或 36 位十六进制 UUID 转换为 36 字符标准格式 (8-4-4-4-12)
   */
  [[nodiscard]] static std::string formatStandardUuid(std::string_view raw_uuid);

  /**
   * @brief 将 32 位或 36 位十六进制 UUID 转换为 16 字节原始大端数组
   */
  [[nodiscard]] static std::optional<std::array<uint8_t, 16>> parseUuidBytes(
      std::string_view raw_uuid
  );

  /**
   * @brief 从 Mojang 属性 JSON 数组解析属性列表，并提取可能的 BungeeGuard token
   */
  static std::vector<PlayerProperty> parsePropertiesJson(
      std::string_view json,
      std::string* out_guard_token = nullptr
  );
};

}  // namespace mcpe::protocol

#endif  // MCPE_PROTOCOL_BUNGEECORD_H
