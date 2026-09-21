#include "mcpe/protocol/StatusHandler.h"

#include <spdlog/spdlog.h>

#include <format>

#include "mcpe/network/TcpSession.h"
#include "mcpe/protocol/ConnectionState.h"

namespace mcpe::protocol {

void
StatusHandler::handleHandshake(
    const std::shared_ptr<network::TcpSession>& session,
    ByteBuffer& payload
) {}

void
StatusHandler::handleStatusRequest(const std::shared_ptr<network::TcpSession>& session) {}

void
StatusHandler::handlePing(
    const std::shared_ptr<network::TcpSession>& session,
    ByteBuffer& payload
) {}

}  // namespace mcpe::protocol
