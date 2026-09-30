#include "common/VarInt.h"

namespace mcpe {

/**
 * @brief 将 DecodeError 转换为人类可读的描述字符串
 * @param error 解码错误类型
 * @return 错误说明字符串（如 "kIncomplete(need more data)"）
 */
const char*
toString(DecodeError error) noexcept {
  switch (error) {
    case DecodeError::kIncomplete:
      return "kIncomplete(need more data)";
    case DecodeError::kMalformed:
      return "kMalformed(protocol limit exceeded)";
  }
  return "kUnknown";
}

}  // namespace mcpe
