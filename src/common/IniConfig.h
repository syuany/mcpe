/**
 * @file IniConfig.h
 * @brief 轻量级零外部依赖 INI 配置文件读写器（Foundations 基础层，对标 Cuberite cIniFile）
 *
 * 核心规范：
 * 1. 支持标准 [Section] 与 key = value 语法，兼容 ';' 与 '#' 注释；
 * 2. 提供 String, Int, Double, Bool, Vector3d 与 AABB 强类型读取及默认值回退；
 * 3. 支持运行期修改配置项（如 /setmaxplayers, /setspawn）并格式化写回磁盘。
 */

#ifndef MCPE_COMMON_INI_CONFIG_H
#define MCPE_COMMON_INI_CONFIG_H

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/SpatialTypes.h"

namespace mcpe::common {

class IniConfig {
 public:
  IniConfig() = default;

  /**
   * @brief 从指定磁盘路径加载并解析 .ini 配置文件
   */
  [[nodiscard]] bool loadFromFile(const std::filesystem::path& path);

  /**
   * @brief 从内存字符串解析 INI 内容
   */
  void parseFromString(std::string_view content);

  /**
   * @brief 将当前配置序列化为标准 INI 文本并持久化写入磁盘
   */
  [[nodiscard]] bool saveToFile(const std::filesystem::path& path) const;

  /**
   * @brief 导出为标准 INI 格式文本
   */
  [[nodiscard]] std::string toString() const;

  [[nodiscard]] bool hasSection(std::string_view section) const noexcept;
  [[nodiscard]] bool hasKey(std::string_view section, std::string_view key) const noexcept;

  [[nodiscard]] std::string getString(
      std::string_view section,
      std::string_view key,
      std::string_view default_val = ""
  ) const;

  [[nodiscard]] int32_t getInt(
      std::string_view section,
      std::string_view key,
      int32_t default_val = 0
  ) const;

  [[nodiscard]] double getDouble(
      std::string_view section,
      std::string_view key,
      double default_val = 0.0
  ) const;

  [[nodiscard]] bool getBool(
      std::string_view section,
      std::string_view key,
      bool default_val = false
  ) const;

  /**
   * @brief 解析形如 "x, y, z" 的三维坐标配置项
   */
  [[nodiscard]] std::optional<Vector3d> getVector3d(
      std::string_view section,
      std::string_view key
  ) const;

  /**
   * @brief 解析由 min_key 与 max_key 构成的三维 AABB 包围盒
   */
  [[nodiscard]] std::optional<AABB> getAABB(
      std::string_view section,
      std::string_view min_key,
      std::string_view max_key
  ) const;

  void setString(std::string_view section, std::string_view key, std::string_view value);
  void setInt(std::string_view section, std::string_view key, int32_t value);
  void setDouble(std::string_view section, std::string_view key, double value);
  void setBool(std::string_view section, std::string_view key, bool value);
  void setVector3d(std::string_view section, std::string_view key, const Vector3d& vec);

  /**
   * @brief 工具函数：按逗号分隔字符串并去除首尾空白
   */
  [[nodiscard]] static std::vector<std::string> splitCsv(std::string_view csv);
  [[nodiscard]] static std::string_view trim(std::string_view sv) noexcept;

 private:
  std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> data_;
};

}  // namespace mcpe::common

namespace mcpe {
using IniConfig = common::IniConfig;
}

#endif  // MCPE_COMMON_INI_CONFIG_H
