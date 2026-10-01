#include "network/TcpSession.h"

#include <spdlog/spdlog.h>

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <cstddef>
#include <utility>

#include "common/ByteBuffer.h"
#include "network/PacketFraming.h"

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
  // 使用 asio::co_spawn 将协程投递到底层 socket 所在的 executor 上并发运行
  asio::co_spawn(
      socket_.get_executor(),
      [self = shared_from_this()]() {
        return self->readerLoop();
      },
      asio::detached  // asio::detached 表示分离执行，协程自生自灭，异常与退出自主管理
  );
}

void
TcpSession::stop() {
  // 保证幂等：若已经处于停止状态则直接返回，防止并发或重复关闭
  if (is_stopped_) return;
  is_stopped_ = true;

  read_buffer_.recycle();

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

asio::awaitable<void>
TcpSession::readerLoop() {
  auto self = shared_from_this();

  while (!is_stopped_) {
    auto [ec, bytes_transferred] = co_await socket_.async_read_some(
        read_buffer_.prepareWriteBuffer(),
        asio::as_tuple(asio::use_awaitable)
    );

    if (ec) {
      // 处理断开连接与网络异常
      if (ec == asio::error::eof || ec == asio::error::connection_reset) {
        spdlog::info("[Session] Client disconnected: {}", remote_address_);
      } else if (ec != asio::error::operation_aborted) {
        spdlog::warn("[Session] Read error on {}: {}", remote_address_, ec.message());
      }
      read_buffer_.recycle();
      break;
    }

    // 1. 推进缓冲区写游标（内核直写物理块，0 次额外用户态拷贝）
    read_buffer_.commit(bytes_transferred);

    // 2. 循环拆包：应对 TCP 粘包（一次收到多个包）和半包（数据包未收全）
    bool has_malformed_packet = false;
    while (true) {
      auto frame_res = PacketFraming::slicePacketFromView(read_buffer_.unconsumedView());
      if (!frame_res) {
        if (frame_res.error() == FramingError::kIncomplete) {
          // 半包：就地碎片整理并等待后续 TCP 读取
          read_buffer_.compactIfIncomplete();
          break;
        }
        // 畸形包（包长超过上限或非法负数）：视为恶意攻击，立即踢出并记录日志
        spdlog::error("[Session] Malformed packet from {}, disconnect!", remote_address_);
        has_malformed_packet = true;
        break;
      }

      // 划过该整包字节（若整块刚好被切包读完，内部立即触发 recycle() 恢复 0 字节占用）
      read_buffer_.consume(frame_res->consumed_bytes);

      spdlog::debug(
          "[Session] Sliced complete packet with {} bytes from {}",
          frame_res->packet.readableBytes(),
          remote_address_
      );

      // 通过注册的回调函数将数据包分发至协议层（保持网络层与协议层解耦）
      if (packet_handler_) {
        packet_handler_(shared_from_this(), std::move(frame_res->packet));
      }
    }
    if (has_malformed_packet) break;
  }
  stop();
  co_return;
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
      socket_,
      asio::buffer(front_buf.peek(), front_buf.readableBytes()),
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
