#include "network/TcpServer.h"

#include <spdlog/spdlog.h>

#include <asio/co_spawn.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/use_awaitable.hpp>
#include <functional>
#include <memory>

#include "network/TcpSession.h"

namespace mcpe::network {

TcpServer::TcpServer(asio::io_context& io_context, uint16_t port)
    : io_context_(io_context),
      acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
  spdlog::info("[TcpServer] Listening on 0.0.0.0:{}", port);
}

void
TcpServer::start() {
  is_running_ = true;
  asio::co_spawn(
      acceptor_.get_executor(),
      std::bind_front(&TcpServer::listenLoop, this),
      asio::detached
  );
}

void
TcpServer::stop() {
  is_running_ = false;
  asio::error_code ec;
  // 关闭 acceptor 释放监听端口并取消所有排队的 accept 异步操作
  acceptor_.close(ec);
}

asio::awaitable<void>
TcpServer::listenLoop() {
  while (is_running_) {
    // 服务器已停止时，丢弃回调并不再继续注册新的 accept
    auto [ec, socket] = co_await acceptor_.async_accept(asio::as_tuple(asio::use_awaitable));
    if (ec) {
      if (ec != asio::error::operation_aborted) {
        spdlog::error("[TcpServer] Accept error: {}", ec.message());
      }
      break;
    }
    // 开启 TCP_NODELAY，禁用 Nagle 算法，确保小包（如 Ping、微小移动包）无缓冲即时发出
    asio::error_code opt_ec;
    socket.set_option(asio::ip::tcp::no_delay(true), opt_ec);

    // 创建客户端会话生命周期对象并配置事件监听接口
    auto session = std::make_shared<TcpSession>(std::move(socket));
    if (session_factory_) {
      auto cb = session_factory_(session);
      session->setCallbacks(std::move(cb));
    } else if (callbacks_) {
      session->setCallbacks(callbacks_);
    } else if (packet_handler_) {
      session->setPacketHandler(packet_handler_);
    }
    session->start();
  }
  co_return;
}

}  // namespace mcpe::network
