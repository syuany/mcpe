#include "network/TcpSession.h"

#include <spdlog/spdlog.h>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/post.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>
#include <utility>

#include "common/ByteBuffer.h"
#include "network/PacketFraming.h"

namespace mcpe::network {

TcpSession::TcpSession(asio::ip::tcp::socket socket)
    : socket_(std::move(socket)), keep_alive_timer_(socket_.get_executor()) {
  asio::error_code ec;
  const auto endpoint = socket_.remote_endpoint(ec);
  if (!ec) {
    remote_address_ = fmt::format("{}:{}", endpoint.address().to_string(), endpoint.port());
  } else {
    remote_address_ = "unknown";
  }
  spdlog::info("[Session] Connected from {}", remote_address_);
  last_alive_time_ = std::chrono::steady_clock::now();
}

TcpSession::~TcpSession() { spdlog::info("[Session] Destroyed: {}", remote_address_); }

void
TcpSession::start() {
  asio::co_spawn(
      socket_.get_executor(),
      [self = shared_from_this()]() {
        return self->readerLoop();
      },
      asio::detached
  );
}

void
TcpSession::stop() {
  if (is_stopped_) return;
  is_stopped_ = true;

  asio::error_code ec;
  // 取消定时器，促使挂起中的 keepAliveLoop 协程立即唤醒退出
  keep_alive_timer_.cancel(ec);
  socket_.close(ec);
  if (ec) {
    spdlog::debug("[Session] Error closing socket {}: {}", remote_address_, ec.message());
  }
}

void
TcpSession::sendPacket(ByteBuffer buffer) {
  if (is_stopped_ || buffer.readableBytes() == 0) return;

  // 投递至 socket executor 串行执行，避免多线程并发 async_write
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

asio::awaitable<void>
TcpSession::readerLoop() {
  auto self = shared_from_this();

  while (!is_stopped_) {
    // 预留可写空间后直接读入累积缓冲区，避免额外 memcpy
    cumulative_buf_.ensureWritableBytes(4096);

    auto [ec, bytes_transferred] = co_await socket_.async_read_some(
        asio::buffer(cumulative_buf_.beginWrite(), cumulative_buf_.writableBytes()),
        asio::as_tuple(asio::use_awaitable)
    );

    if (ec) {
      if (ec == asio::error::eof || ec == asio::error::connection_reset) {
        spdlog::info("[Session] Client disconnected: {}", remote_address_);
      } else if (ec != asio::error::operation_aborted) {
        spdlog::warn("[Session] Read error on {}: {}", remote_address_, ec.message());
      }
      break;
    }

    cumulative_buf_.hasWritten(bytes_transferred);

    // 循环拆包处理粘包
    bool has_malformed_packet = false;
    while (true) {
      auto frame_res = PacketFraming::slicePacket(cumulative_buf_);
      if (!frame_res) {
        if (frame_res.error() == FramingError::kIncomplete) {
          // 半包：等待后续网络数据到达
          break;
        }
        // 畸形包：协议非法，断开连接防恶意攻击
        spdlog::error("[Session] Malformed packet from {}, disconnect!", remote_address_);
        has_malformed_packet = true;
        break;
      }

      ByteBuffer packet = std::move(*frame_res);
      spdlog::debug(
          "[Session] Sliced complete packet with {} bytes from {}",
          packet.readableBytes(),
          remote_address_
      );

      if (packet_handler_) {
        packet_handler_(shared_from_this(), std::move(packet));
      }
    }
    if (has_malformed_packet) break;
  }

  stop();
  co_return;
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
      socket_,
      asio::buffer(front_buf.peek(), front_buf.readableBytes()),
      [this, self](const asio::error_code& ec, std::size_t bytes_transferred) {
        (void)bytes_transferred;
        if (is_stopped_) return;

        if (!ec) {
          // 当前包发送完成，弹出并链式驱动下一个包
          write_queue_.pop_front();
          doWrite();
        } else {
          if (ec != asio::error::operation_aborted) {
            spdlog::warn("[Session] Write error to {}: {}", remote_address_, ec.message());
          }
          stop();
        }
      }
  );
}

void
TcpSession::startKeepAliveTimer() {
  last_alive_time_ = std::chrono::steady_clock::now();
  // 启动独立的保活心跳协程
  asio::co_spawn(
      socket_.get_executor(),
      std::bind_front(&TcpSession::keepAliveLoop, shared_from_this()),
      asio::detached
  );
}

asio::awaitable<void>
TcpSession::keepAliveLoop() {
  // 在协程帧中持有 shared_ptr，生命周期自保
  auto self = shared_from_this();

  while (!is_stopped_) {
    keep_alive_timer_.expires_after(std::chrono::seconds(10));
    // 异步挂起 10 秒，等待定时器到期或被 cancel 唤醒
    auto [ec] = co_await keep_alive_timer_.async_wait(asio::as_tuple(asio::use_awaitable));
    if (is_stopped_ || ec) {
      break;  // 连接已断开或定时器被 cancel，优雅退出协程
    }

    // 超时看门狗：检查是否超过 20 秒未收到任何客户端心跳响应
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_alive_time_).count();
    if (elapsed > 20) {
      spdlog::warn(
          "[Session] Client {} keep-alive timeout ({}s > 20s), kicking!",
          remote_address_,
          elapsed
      );
      stop();
      break;
    }

    sendKeepAlive();
  }
  co_return;
}

void
TcpSession::sendKeepAlive() {
  // 使用当前毫秒时间戳作为心跳 ID
  const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch()
  )
                             .count();
  last_keep_alive_id_ = now_ms;
  awaiting_keep_alive_ = true;

  ByteBuffer packet;
  packet.appendVarInt(0x1F);        // Packet ID = 0x1F (Keep Alive clientbound)
  packet.writeBe<int64_t>(now_ms);  // 8 字节大端整数 Long

  // 利用 Cheap Prepend 原位前插包长 VarInt 前缀（零内存拷贝）
  packet.prependVarInt(packet.readableBytes());
  sendPacket(std::move(packet));

  spdlog::debug("[Session] Sent Keep Alive 0x1F to {}, ID: {}", remote_address_, now_ms);
}

void
TcpSession::onKeepAliveResponse(int64_t id) {
  if (id == last_keep_alive_id_) {
    last_alive_time_ = std::chrono::steady_clock::now();
    awaiting_keep_alive_ = false;
    spdlog::debug("[Session] Received valid Keep Alive echo from {}, ID: {}", remote_address_, id);
  } else {
    spdlog::warn(
        "[Session] Keep alive ID mismatch from {}: expected {}, got {}",
        remote_address_,
        last_keep_alive_id_,
        id
    );
  }
}

}  // namespace mcpe::network
