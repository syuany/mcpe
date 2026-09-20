#include "mcpe/common/VarInt.h"

namespace mcpe {

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
