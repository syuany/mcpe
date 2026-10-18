#include "protocol/ClientSession.h"

#include <spdlog/spdlog.h>

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <atomic>
#include <utility>

#include "lobby/LobbyManager.h"
#include "network/PacketWriter.h"
#include "network/TcpSession.h"
#include "protocol/ClientManager.h"
#include "protocol/PacketRouter.h"
#include "protocol/PlayHandler.h"
#include "world/EventQueue.h"

namespace mcpe::protocol {

namespace {
std::atomic<uint64_t> g_next_session_id{1};
std::atomic<int32_t> g_next_entity_id{1};
}  // namespace

void
ClientSession::startPlay() {
  setState(ConnectionState::PLAY);
  if (entity_id_ == 0) {
    entity_id_ = g_next_entity_id.fetch_add(1, std::memory_order_relaxed);
  }
  PlayHandler::onPlayerJoin(shared_from_this());
}

ClientSession::ClientSession(std::shared_ptr<network::TcpSession> tcp_session)
    : tcp_session_(tcp_session),
      remote_address_(tcp_session ? tcp_session->remoteAddress() : "unknown"),
      keep_alive_timer_(tcp_session->socket().get_executor()),
      protocol_handler_(std::make_shared<PacketRouter>()),
      session_id_(g_next_session_id.fetch_add(1, std::memory_order_relaxed)) {
  last_alive_time_ = std::chrono::steady_clock::now();
  spdlog::info("[ClientSession] Created for {} (SID: {})", remote_address_, session_id_);
}

ClientSession::~ClientSession() {
  cancelKeepAlive();
  spdlog::info("[ClientSession] Destroyed for {}", remote_address_);
}

void
ClientSession::onPacket(
    const std::shared_ptr<network::TcpSession>& /*session*/,
    ByteBuffer packet
) {
  if (protocol_handler_) {
    protocol_handler_->handlePacket(shared_from_this(), std::move(packet));
  }
}

void
ClientSession::onDisconnected(const std::shared_ptr<network::TcpSession>& /*session*/) {
  cancelKeepAlive();
  if (auto lobby_mgr = PlayHandler::getLobbyManager()) {
    lobby_mgr->onPlayerDisconnected(session_id_, entity_id_);
  } else if (auto q = eventQueue()) {
    world::WorldEvent ev;
    ev.session_id = session_id_;
    ev.entity_id = entity_id_;
    ev.payload = world::PlayerLeaveEvent{};
    q->push(std::move(ev));
  }
  if (auto mgr = client_manager_.lock()) {
    mgr->removeSession(shared_from_this());
  }
  spdlog::info("[ClientSession] Client disconnected: {}", remote_address_);
}

std::shared_ptr<world::EventQueue>
ClientSession::eventQueue() const noexcept {
  std::lock_guard<std::mutex> lock(instance_mutex_);
  return event_queue_.lock();
}

void
ClientSession::sendPacket(ByteBuffer buffer) {
  if (auto tcp = tcp_session_.lock()) {
    tcp->sendPacket(std::move(buffer));
  }
}

void
ClientSession::stop() {
  cancelKeepAlive();
  if (auto tcp = tcp_session_.lock()) {
    tcp->stop();
  }
}

bool
ClientSession::isStopped() const noexcept {
  auto tcp = tcp_session_.lock();
  return !tcp || tcp->isStopped();
}

const std::string&
ClientSession::remoteAddress() const noexcept {
  return remote_address_;
}

std::shared_ptr<network::TcpSession>
ClientSession::tcpSession() const noexcept {
  return tcp_session_.lock();
}

std::shared_ptr<ClientManager>
ClientSession::clientManager() const noexcept {
  return client_manager_.lock();
}

void
ClientSession::cancelKeepAlive() {
  asio::error_code ec;
  keep_alive_timer_.cancel(ec);
}

void
ClientSession::startKeepAliveTimer() {
  last_alive_time_ = std::chrono::steady_clock::now();
  asio::co_spawn(
      keep_alive_timer_.get_executor(),
      std::bind_front(&ClientSession::keepAliveLoop, shared_from_this()),
      asio::detached
  );
}

asio::awaitable<void>
ClientSession::keepAliveLoop() {
  auto self = shared_from_this();

  while (!isStopped()) {
    keep_alive_timer_.expires_after(std::chrono::seconds(10));
    auto [ec] = co_await keep_alive_timer_.async_wait(asio::as_tuple(asio::use_awaitable));
    if (isStopped() || ec) {
      break;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - last_alive_time_).count();
    const auto timeout_sec = ServerConfig::defaultInstance().keep_alive_timeout_seconds;
    if (elapsed > timeout_sec) {
      spdlog::warn(
          "[ClientSession] Client {} keep-alive timeout ({}s > {}s), kicking!",
          remoteAddress(),
          elapsed,
          timeout_sec
      );
      stop();
      break;
    }

    sendKeepAlive();
  }
  co_return;
}

void
ClientSession::sendKeepAlive() {
  const int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch()
  )
                             .count();
  last_keep_alive_id_ = now_ms;
  awaiting_keep_alive_ = true;

  // 使用 RAII PacketWriter 自动打包并安全发送 0x1F Keep Alive
  network::PacketWriter writer(shared_from_this(), 0x1F);
  writer.writeBe<int64_t>(now_ms);

  spdlog::debug("[ClientSession] Sent Keep Alive 0x1F to {}, ID: {}", remoteAddress(), now_ms);
}

void
ClientSession::onKeepAliveResponse(int64_t id) {
  if (id == last_keep_alive_id_) {
    last_alive_time_ = std::chrono::steady_clock::now();
    awaiting_keep_alive_ = false;
    spdlog::debug(
        "[ClientSession] Received valid Keep Alive echo from {}, ID: {}",
        remoteAddress(),
        id
    );
  } else {
    spdlog::warn(
        "[ClientSession] Keep alive ID mismatch from {}: expected {}, got {}",
        remoteAddress(),
        last_keep_alive_id_,
        id
    );
  }
}

void
ClientSession::connectServer(std::string_view target_server) {
  if (isStopped()) {
    return;
  }
  spdlog::info(
      "[ClientSession] Forwarding player '{}' (SID: {}) to server '{}' via BungeeCord",
      username_,
      session_id_,
      target_server
  );

  network::PacketWriter writer(shared_from_this(), ClientboundPacket::PluginMessage);
  writer.appendString("BungeeCord");
  writer.appendJavaUtf("Connect");
  writer.appendJavaUtf(target_server);
}

}  // namespace mcpe::protocol
