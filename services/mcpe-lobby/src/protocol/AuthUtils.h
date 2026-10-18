#ifndef AUTHUTILS_H
#define AUTHUTILS_H

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace mcpe::protocol {

class AuthUtils {
 public:
  /**
   * @brief 校验玩家用户名是否符合 Minecraft 原版规范
   * 规范：1~16 个字符，仅包含 a-z, A-Z, 0-9, _
   */
  static bool isValidUsername(std::string_view name) noexcept;

  /**
   * @brief 计算符合 RFC-4122 v3 标准的离线模式玩家 UUID
   * 计算公式: MD5("OfflinePlayer:" + username) 并设置 version=3 与 IETF variant
   *
   * @param username 玩家名 (如 "Steve")
   * @return 36 字符的标准 UUID 字符串 (如 "5627dd98-e6be-3c21-b8a8-e92344183641")
   */
  static std::string generateOfflineUuid(std::string_view username);

  /**
   * @brief 计算符合 RFC-4122 v3 标准的离线模式玩家 UUID 原始 16 字节
   */
  static std::array<uint8_t, 16> generateOfflineUuidBytes(std::string_view username);

  /**
   * @brief 将标准 36 字符 UUID 字符串反序列化为原始 16 字节数组
   */
  static std::array<uint8_t, 16> parseUuidStringToBytes(std::string_view uuid_str);
};

}  // namespace mcpe::protocol

#endif  // AUTHUTILS_H