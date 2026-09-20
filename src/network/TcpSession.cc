#include "mcpe/network/TcpSession.h"

#include <asio/error_code.hpp>
#include <spdlog/spdlog.h>

namespace mcpe::network {

TcpSession::TcpSession(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)) {
  asio::error_code ec;
  const auto endpoint = socket_.remote_endpoint(ec);
  if (!ec) {
    remote_address_ = fmt::format("{}:{}", endpoint.address().to_string(), endpoint.port());
  } else {
    remote_address_ = "unknown";
  }
  spdlog::info("[Session] Connected from {}", remote_address_);
}

TcpSession::~TcpSession() {
  spdlog::info("[Session] Destroyed: {}", remote_address_);
}

void TcpSession::start() {
  doRead();
}

void TcpSession::stop() {
  if (is_stopped_) return;
  is_stopped_ = true;

  asio::error_code ec;
  socket_.close(ec);
  if (ec) {
    spdlog::debug("[Session] Error closing socket {}: {}", remote_address_, ec.message());
  }
}

void TcpSession::doRead() {
  auto self = shared_from_this();
  socket_.async_read_some(
      asio::buffer(read_buffer_),
      [this, self](const asio::error_code& ec, std::size_t bytes_transferred) {
        if (is_stopped_) return;

        if (!ec) {
          spdlog::debug("[Session] Read {} bytes from {}", bytes_transferred, remote_address_);
          doRead();
        } else {
          if (ec == asio::error::eof || ec == asio::error::connection_reset) {
            spdlog::info("[Session] Client disconnected: {}", remote_address_);
          } else {
            spdlog::warn("[Session] Read error on {}: {}", remote_address_, ec.message());
          }
          stop();
        }
      });
}

}  // namespace mcpe::network
