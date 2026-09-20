#ifndef TCPSERVER_H
#define TCPSERVER_H

#include <netdb.h>

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <cstdint>

namespace mcpe::network {

class TcpServer {
 public:
  TcpServer(asio::io_context& io_context, uint16_t port);
  ~TcpServer() = default;

  void start();
  void stop();

 private:
  void doAccept();

  asio::io_context& io_context_;
  asio::ip::tcp::acceptor acceptor_;
  bool is_running_{false};
};

}  // namespace mcpe::network

#endif  // TCPSERVER_H