#ifndef PACKETROUTER_H
#define PACKETROUTER_H

#include <memory>

#include "mcpe/common/ByteBuffer.h"
#include "mcpe/protocol/ConnectionState.h"

namespace mcpe::network {
class TcpSession;
}

namespace mcpe::protocol {

class PacketRouter {
 public:
  static void dispatch(
      const std::shared_ptr<network::TcpSession>& session,
      ConnectionState state,
      ByteBuffer packet
  );
};

}  // namespace mcpe::protocol

#endif  // PACKETROUTER_H