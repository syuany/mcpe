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

void
TcpServer::start() {
  is_running_ = true;
  doAccept();
}

void
TcpServer::stop() {
  is_running_ = false;
  asio::error_code ec;
  // 关闭 acceptor 释放监听端口并取消所有排队的 accept 异步操作
  acceptor_.close(ec);
}

void
TcpServer::doAccept() {
  acceptor_.async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
    // 服务器已停止时，丢弃回调并不再继续注册新的 accept
    if (!is_running_) return;

    if (!ec) {
      // 开启 TCP_NODELAY，禁用 Nagle 算法，确保小包（如 Ping、微小移动包）无缓冲即时发出
      asio::error_code opt_ec;
      socket.set_option(asio::ip::tcp::no_delay(true), opt_ec);

      // 创建客户端会话生命周期对象并启动异步读循环
      auto session = std::make_shared<TcpSession>(std::move(socket));
      if (packet_handler_) {
        session->setPacketHandler(packet_handler_);
      }
      session->start();
    } else {
      spdlog::error("[TcpServer] Accept error: {}", ec.message());
    }

    // 继续链式注册下一个 accept 操作，维持并发监听
    doAccept();
  });
}

}  // namespace mcpe::network
