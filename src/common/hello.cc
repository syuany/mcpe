#include "mcpe/common/hello.h"

#include <spdlog/spdlog.h>

namespace mcpe {
std::string get_greeting() {
  spdlog::info("Hello from mcpe_common library!");
  return "Hello, MCPE!";
}
}  // namespace mcpe