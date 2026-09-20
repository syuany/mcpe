#include "mcpe/network/TcpSession.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <asio/error_code.hpp>
#include <asio/write.hpp>
#include <cstddef>

namespace mcpe::network {

TcpSession::TcpSession(asio::ip::tcp::socket socket) : socket_(std::move(socket)) {
  asio::error_code ec;
  const auto endpoint = socket_.remote_endpoint(ec);
  if (!ec) {
    remote_address_ = fmt::format("{}:{}", endpoint.address().to_string(), endpoint.port());
  } else {
    remote_address_ = "unknown";
  }
  spdlog::info("[Session] Connected from {}", remote_address_);
}

TcpSession::~TcpSession() { spdlog::info("[Session] Destroyed: {}", remote_address_); }

void
TcpSession::start() {
  doRead();
}

void
TcpSession::stop() {
  if (is_stopped_) return;
  is_stopped_ = true;

  asio::error_code ec;
  socket_.close(ec);
  if (ec) {
    spdlog::debug("[Session] Error closing socket {}: {}", remote_address_, ec.message());
  }
}

void
TcpSession::sendPacket(ByteBuffer buffer) {
  if (is_stopped_ || buffer.readableBytes() == 0) return;
  asio::post(
      socket_.get_executor(),
      [this, self = shared_from_this(), buf = std::move(buffer)]() mutable {
        if (is_stopped_) return;

        write_queue_.push_back(std::move(buf));
        if (!is_writing_) {
          is_writing_ = true;
          doWrite();
        }
      }
  );
}

void
TcpSession::doRead() {
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
      }
  );
}

void
TcpSession::doWrite() {
  if (write_queue_.empty()) {
    is_writing_ = false;
    return;
  }

  const auto& front_buf = write_queue_.front();
  auto self = shared_from_this();

  asio::async_write(
      socket_, asio::buffer(front_buf.peek(), front_buf.readableBytes()),
      [this, self](const asio::error_code& ec, std::size_t bytes_transferred) {
        if (is_stopped_) return;

        if (!ec) {
          write_queue_.pop_front();
          doWrite();
        } else {
          spdlog::warn("[Session] Write error to {}: {}", remote_address_, ec.message());
          stop();
        }
      }
  );
}

}  // namespace mcpe::network
