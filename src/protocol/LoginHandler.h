#ifndef LOGINHANDLER_H
#define LOGINHANDLER_H

#include <memory>

#include "common/ByteBuffer.h"

namespace mcpe::protocol {

class ClientSession;

class LoginHandler {
 public:
  /**
   * @brief 处理 LOGIN 阶段的 Login Start (0x00) 包
   */
  static void
  handleLoginStart(const std::shared_ptr<ClientSession>& session, ByteBuffer& payload);
};

}  // namespace mcpe::protocol

#endif  // LOGINHANDLER_H