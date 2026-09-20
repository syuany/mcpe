#ifndef TCPSESSION_H
#define TCPSESSION_H

#include <array>
#include <asio/ip/tcp.hpp>
#include <memory>
#include <string>

namespace mcpe::network {

class TcpSession : public std::enable_shared_from_this<TcpSession> {
 public:
  explicit TcpSession(asio::ip::tcp::socket socket);
  ~TcpSession();

  TcpSession(const TcpSession&) = delete;
  TcpSession& operator=(const TcpSession&) = delete;

  void start();

  void stop();

  asio::ip::tcp::socket&
  socket() noexcept {
    return socket_;
  }

  const std::string&
  remoteAddress() const noexcept {
    return remote_address_;
  }

 private:
  void doRead();

  asio::ip::tcp::socket socket_;
  std::string remote_address_;

  std::array<char, 4096> read_buffer_;

  bool is_stopped_{false};
};

}  // namespace mcpe::network

#endif  // TCPSESSION_H