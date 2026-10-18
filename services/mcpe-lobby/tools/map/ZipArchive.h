/**
 * @file ZipArchive.h
 * @brief 轻量级 ZIP 归档读取器（Foundations 基础层）
 *
 * 核心规范：
 * 1. 纯 C++23 与标准 Zlib，零引入额外第三方外部重型依赖；
 * 2. 支持标准 PKZIP Central Directory 解析，支持 Stored (0) 与 Deflated (8) 压缩算法；
 * 3. 支持文件列表枚举、基于扩展名查找与指定条目流式解压至内存缓冲区；
 * 4. 极致内存安全，全流程防越界与防恶意 zip 炸弹边界保护。
 */

#ifndef MCPE_COMMON_ZIP_ARCHIVE_H
#define MCPE_COMMON_ZIP_ARCHIVE_H

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mcpe::common {

/**
 * @brief ZIP 归档条目元数据
 */
struct ZipEntry {
  std::string name;
  uint16_t compression_method{0};  // 0 = Stored, 8 = Deflated
  uint32_t compressed_size{0};
  uint32_t uncompressed_size{0};
  uint32_t local_header_offset{0};
  bool is_directory{false};
};

/**
 * @brief 轻量级只读 ZIP 归档解析器
 */
class ZipArchive {
 public:
  ZipArchive() = default;
  ~ZipArchive() = default;

  /**
   * @brief 从本地磁盘文件打开 ZIP 归档
   */
  [[nodiscard]] static std::optional<ZipArchive> openFile(const std::filesystem::path& file_path);

  /**
   * @brief 从内存字节缓冲区直接打开 ZIP 归档
   */
  [[nodiscard]] static std::optional<ZipArchive> openMemory(std::span<const uint8_t> buffer);

  /**
   * @brief 检查归档内是否存在指定文件名的条目
   */
  [[nodiscard]] bool hasEntry(std::string_view entry_name) const;

  /**
   * @brief 获取归档内所有条目元信息
   */
  [[nodiscard]] const std::vector<ZipEntry>& entries() const noexcept {
    return entries_;
  }

  /**
   * @brief 获取归档内所有文件的相对路径列表
   */
  [[nodiscard]] std::vector<std::string> entryNames() const;

  /**
   * @brief 按文件扩展名查找首个匹配条目（例如 ".schematic"）
   */
  [[nodiscard]] std::optional<std::string> findFirstByExtension(std::string_view extension) const;

  /**
   * @brief 解压指定 entry 文件的完整字节流至内存
   */
  [[nodiscard]] std::optional<std::vector<uint8_t>> extractEntry(std::string_view entry_name) const;

 private:
  std::vector<uint8_t> data_;
  std::vector<ZipEntry> entries_;

  [[nodiscard]] bool parseCentralDirectory();
};

}  // namespace mcpe::common

#endif  // MCPE_COMMON_ZIP_ARCHIVE_H
