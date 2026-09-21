#include "mcpe/network/TcpSession.h"

#include <spdlog/spdlog.h>

#include <asio/error_code.hpp>
#include <asio/write.hpp>
#include <cstddef>
#include <utility>

#include "mcpe/common/ByteBuffer.h"
#include "mcpe/network/PacketFraming.h"

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
  // 开启异步读循环，开始接收客户端发来的网络字节流
  doRead();
}

void
TcpSession::stop() {
  // 保证幂等：若已经处于停止状态则直接返回，防止并发或重复关闭
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
  // 若连接已关闭或包体为空，直接丢弃
  if (is_stopped_ || buffer.readableBytes() == 0) return;

  // 使用 asio::post 将发送任务调度至底层 socket 绑定的 executor/strand
  // 解决多线程环境下并发直接发起 async_write 导致 Asio 内部断言崩溃的问题
  asio::post(
      socket_.get_executor(),
      [this, self = shared_from_this(), buf = std::move(buffer)]() mutable {
        if (is_stopped_) return;

        write_queue_.push_back(std::move(buf));
        // 若当前没有正在进行的异步写任务，则由当前投递的任务唤醒 doWrite 进行链式发送
        if (!is_writing_) {
          is_writing_ = true;
          doWrite();
        }
      }
  );
}

void
TcpSession::doRead() {
  // 异步读取任意可用的字节数据到固定大小的 read_buffer_ 中
  socket_.async_read_some(
      asio::buffer(read_buffer_),
      [this, self = shared_from_this()](const asio::error_code& ec, std::size_t bytes_transferred) {
        if (is_stopped_) return;

        if (!ec) {
          // 1. 将本次读取到的原始字节追加到累积接收缓冲区
          cumulative_buf_.append(read_buffer_.data(), bytes_transferred);

          // 2. 循环拆包：应对 TCP 粘包（一次收到多个包）和半包（数据包未收全）
          while (true) {
            auto frame_res = PacketFraming::slicePacket(cumulative_buf_);
            if (!frame_res) {
              if (frame_res.error() == FramingError::kIncomplete) {
                // 半包：数据未到齐，跳出循环等待下一次 TCP 读取
                break;
              }
              // 畸形包（包长超过上限或非法负数）：视为恶意攻击，立即踢出并记录日志
              spdlog::error("[Session] Malformed packet from {}, disconnect!", remote_address_);
              stop();
              return;
            }

            // 成功切出一个完整 Minecraft 协议包
            ByteBuffer packet = std::move(*frame_res);
            const std::size_t packet_size = packet.readableBytes();

            spdlog::debug(
                "[Session] Sliced complete packet with {} bytes from {}", packet_size,
                remote_address_
            );

            // 通过注册的回调函数将数据包分发至协议层（保持网络层与协议层解耦）
            if (packet_handler_) {
              packet_handler_(shared_from_this(), std::move(packet));
            }
          }
          // 3. 继续监听后续网络输入
          doRead();
        } else {
          // 处理断开连接与网络异常
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
  // 若队列已空，将 is_writing_ 置为 false 并退出链式调用
  if (write_queue_.empty()) {
    is_writing_ = false;
    return;
  }

  // 取出队首待发送数据包进行异步写
  const auto& front_buf = write_queue_.front();
  auto self = shared_from_this();

  asio::async_write(
      socket_, asio::buffer(front_buf.peek(), front_buf.readableBytes()),
      [this, self](const asio::error_code& ec, std::size_t bytes_transferred) {
        (void)bytes_transferred;
        if (is_stopped_) return;

        if (!ec) {
          // 当前包已全部写入操作系统 Socket 缓冲区，弹出队头并驱动下一个包
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
