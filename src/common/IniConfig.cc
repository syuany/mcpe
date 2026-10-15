/**
 * @file IniConfig.cc
 * @brief 轻量级零外部依赖 INI 配置文件读写器实现
 */

#include "common/IniConfig.h"

#include <charconv>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

namespace mcpe::common {

std::string_view
IniConfig::trim(std::string_view sv) noexcept {
  while (!sv.empty() &&
         (sv.front() == ' ' || sv.front() == '\t' || sv.front() == '\r' || sv.front() == '\n')) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() &&
         (sv.back() == ' ' || sv.back() == '\t' || sv.back() == '\r' || sv.back() == '\n')) {
    sv.remove_suffix(1);
  }
  return sv;
}

std::vector<std::string>
IniConfig::splitCsv(std::string_view csv) {
  std::vector<std::string> items;
  size_t start = 0;
  while (start <= csv.size()) {
    const size_t comma = csv.find(',', start);
    const std::string_view token =
        (comma == std::string_view::npos) ? csv.substr(start) : csv.substr(start, comma - start);
    const std::string_view trimmed = trim(token);
    if (!trimmed.empty()) {
      items.emplace_back(trimmed);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return items;
}

bool
IniConfig::loadFromFile(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return false;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
  parseFromString(ss.str());
  return true;
}

void
IniConfig::parseFromString(std::string_view content) {
  data_.clear();
  std::string current_section = "General";

  size_t pos = 0;
  while (pos < content.size()) {
    const size_t next_nl = content.find('\n', pos);
    std::string_view line =
        (next_nl == std::string_view::npos) ? content.substr(pos) : content.substr(pos, next_nl - pos);
    pos = (next_nl == std::string_view::npos) ? content.size() : (next_nl + 1);

    line = trim(line);
    if (line.empty() || line.front() == ';' || line.front() == '#') {
      continue;
    }

    if (line.front() == '[' && line.back() == ']') {
      std::string_view sec = trim(line.substr(1, line.size() - 2));
      if (!sec.empty()) {
        current_section = std::string(sec);
        (void)data_[current_section];
      }
      continue;
    }

    const size_t eq_pos = line.find('=');
    if (eq_pos == std::string_view::npos) {
      continue;
    }

    const std::string_view key = trim(line.substr(0, eq_pos));
    std::string_view val = trim(line.substr(eq_pos + 1));

    // 若值两侧带对称双引号，自动剥离
    if (val.size() >= 2 && val.front() == '"' && val.back() == '"') {
      val = val.substr(1, val.size() - 2);
    }

    if (!key.empty()) {
      data_[current_section][std::string(key)] = std::string(val);
    }
  }
}

std::string
IniConfig::toString() const {
  std::ostringstream out;
  bool first_sec = true;
  for (const auto& [section, kv_map] : data_) {
    if (!first_sec) {
      out << "\n";
    }
    first_sec = false;
    out << "[" << section << "]\n";
    for (const auto& [k, v] : kv_map) {
      out << k << " = " << v << "\n";
    }
  }
  return out.str();
}

bool
IniConfig::saveToFile(const std::filesystem::path& path) const {
  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  std::ofstream out(path, std::ios::trunc);
  if (!out.is_open()) {
    return false;
  }
  out << toString();
  return out.good();
}

bool
IniConfig::hasSection(std::string_view section) const noexcept {
  return data_.find(section) != data_.end();
}

bool
IniConfig::hasKey(std::string_view section, std::string_view key) const noexcept {
  auto sec_it = data_.find(section);
  if (sec_it == data_.end()) {
    return false;
  }
  return sec_it->second.find(key) != sec_it->second.end();
}

std::string
IniConfig::getString(
    std::string_view section,
    std::string_view key,
    std::string_view default_val
) const {
  auto sec_it = data_.find(section);
  if (sec_it == data_.end()) {
    return std::string(default_val);
  }
  auto key_it = sec_it->second.find(key);
  if (key_it == sec_it->second.end()) {
    return std::string(default_val);
  }
  return key_it->second;
}

int32_t
IniConfig::getInt(std::string_view section, std::string_view key, int32_t default_val) const {
  const std::string raw = getString(section, key, "");
  if (raw.empty()) {
    return default_val;
  }
  int32_t val = default_val;
  const std::string_view sv = trim(raw);
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
  if (ec == std::errc{}) {
    return val;
  }
  return default_val;
}

double
IniConfig::getDouble(std::string_view section, std::string_view key, double default_val) const {
  const std::string raw = getString(section, key, "");
  if (raw.empty()) {
    return default_val;
  }
  char* end = nullptr;
  const double v = std::strtod(raw.c_str(), &end);
  if (end != raw.c_str()) {
    return v;
  }
  return default_val;
}

bool
IniConfig::getBool(std::string_view section, std::string_view key, bool default_val) const {
  const std::string raw = getString(section, key, "");
  if (raw.empty()) {
    return default_val;
  }
  if (raw == "true" || raw == "1" || raw == "yes" || raw == "on") {
    return true;
  }
  if (raw == "false" || raw == "0" || raw == "no" || raw == "off") {
    return false;
  }
  return default_val;
}

std::optional<Vector3d>
IniConfig::getVector3d(std::string_view section, std::string_view key) const {
  const std::string raw = getString(section, key, "");
  if (raw.empty()) {
    return std::nullopt;
  }
  const auto parts = splitCsv(raw);
  if (parts.size() < 3) {
    return std::nullopt;
  }
  char* e0 = nullptr;
  char* e1 = nullptr;
  char* e2 = nullptr;
  const double x = std::strtod(parts[0].c_str(), &e0);
  const double y = std::strtod(parts[1].c_str(), &e1);
  const double z = std::strtod(parts[2].c_str(), &e2);
  if (e0 == parts[0].c_str() || e1 == parts[1].c_str() || e2 == parts[2].c_str()) {
    return std::nullopt;
  }
  return Vector3d{x, y, z};
}

std::optional<AABB>
IniConfig::getAABB(
    std::string_view section,
    std::string_view min_key,
    std::string_view max_key
) const {
  auto min_v = getVector3d(section, min_key);
  auto max_v = getVector3d(section, max_key);
  if (!min_v || !max_v) {
    return std::nullopt;
  }
  return AABB{*min_v, *max_v};
}

void
IniConfig::setString(std::string_view section, std::string_view key, std::string_view value) {
  data_[std::string(section)][std::string(key)] = std::string(value);
}

void
IniConfig::setInt(std::string_view section, std::string_view key, int32_t value) {
  setString(section, key, std::to_string(value));
}

void
IniConfig::setDouble(std::string_view section, std::string_view key, double value) {
  setString(section, key, std::format("{:.2f}", value));
}

void
IniConfig::setBool(std::string_view section, std::string_view key, bool value) {
  setString(section, key, value ? "true" : "false");
}

void
IniConfig::setVector3d(std::string_view section, std::string_view key, const Vector3d& vec) {
  setString(section, key, std::format("{:.2f}, {:.2f}, {:.2f}", vec.x, vec.y, vec.z));
}

}  // namespace mcpe::common
