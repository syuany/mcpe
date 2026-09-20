#include "mcpe/network/TcpServer.h"

#include <asio/error_code.hpp>
#include <memory>

#include "mcpe/network/TcpSession.h"
#include "spdlog/spdlog.h"

namespace mcpe::network {

TcpServer::TcpServer(asio::io_context& io_context, uint16_t port)
    : io_context_(io_context),
      acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
  spdlog::info("[TcpServer] Listening on 0.0.0.0:{}", port);
}

void TcpServer::start() {
  is_running_ = true;
  doAccept();
}

void TcpServer::stop() {
  is_running_ = false;
  asio::error_code ec;
  acceptor_.close(ec);
}

void TcpServer::doAccept() {
  acceptor_.async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
    if (!is_running_) return;

    if (!ec) {
      asio::error_code opt_ec;
      socket.set_option(asio::ip::tcp::no_delay(true), opt_ec);

      auto session = std::make_shared<TcpSession>(std::move(socket));
      session->start();
    } else {
      spdlog::error("[TcpServer] Accept error: {}", ec.message());
    }

    doAccept();
  });
}

}  // namespace mcpe::network
