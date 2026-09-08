#include "mcpe/common/hello.h"

#include <cassert>
#include <iostream>

int main() {
  auto msg = mcpe::get_greeting();
  assert(!msg.empty());
  std::cout << "Test passed: " << msg << std::endl;
  return 0;
}