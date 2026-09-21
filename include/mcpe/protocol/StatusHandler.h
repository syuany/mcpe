#ifndef STATUSHANDLER_H
#define STATUSHANDLER_H

#include <memory>

#include "mcpe/common/ByteBuffer.h"

namespace mcpe::network {
class TcpSession;
}

namespace mcpe::protocol {

class StatusHandler {
 public:
  static void
  handleHandshake(const std::shared_ptr<network::TcpSession>& session, ByteBuffer& payload);

  static void handleStatusRequest(const std::shared_ptr<network::TcpSession>& session);

  static void handlePing(const std::shared_ptr<network::TcpSession>& session, ByteBuffer& payload);
};

}  // namespace mcpe::protocol

#endif  // STATUSHANDLER_H