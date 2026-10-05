#include "protocol/ClientManager.h"

#include <spdlog/spdlog.h>

#include "protocol/ClientSession.h"
#include "protocol/ConnectionState.h"

namespace mcpe::protocol {

void
ClientManager::addSession(std::shared_ptr<ClientSession> session) {
  if (!session) return;
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.insert(std::move(session));
  spdlog::debug("[ClientManager] Session added. Total sessions: {}", sessions_.size());
}

void
ClientManager::removeSession(const std::shared_ptr<ClientSession>& session) {
  if (!session) return;
  std::lock_guard<std::mutex> lock(mutex_);
  sessions_.erase(session);
  spdlog::debug("[ClientManager] Session removed. Total sessions: {}", sessions_.size());
}

size_t
ClientManager::sessionCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return sessions_.size();
}

size_t
ClientManager::playerCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t count = 0;
  for (const auto& session : sessions_) {
    if (session && session->state() == ConnectionState::PLAY) {
      count++;
    }
  }
  return count;
}

void
ClientManager::broadcast(ByteBuffer packet) {
  std::vector<std::shared_ptr<ClientSession>> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& session : sessions_) {
      if (session && session->state() == ConnectionState::PLAY) {
        targets.push_back(session);
      }
    }
  }

  for (size_t i = 0; i < targets.size(); ++i) {
    if (i + 1 == targets.size()) {
      // 最后一个目标直接移动，避免深拷贝
      targets[i]->sendPacket(std::move(packet));
    } else {
      targets[i]->sendPacket(packet.clone());
    }
  }
}

void
ClientManager::broadcastExcept(ByteBuffer packet, const std::shared_ptr<ClientSession>& except) {
  std::vector<std::shared_ptr<ClientSession>> targets;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& session : sessions_) {
      if (session && session != except && session->state() == ConnectionState::PLAY) {
        targets.push_back(session);
      }
    }
  }

  for (size_t i = 0; i < targets.size(); ++i) {
    if (i + 1 == targets.size()) {
      targets[i]->sendPacket(std::move(packet));
    } else {
      targets[i]->sendPacket(packet.clone());
    }
  }
}

void
ClientManager::kickAll() {
  std::vector<std::shared_ptr<ClientSession>> copy;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    copy.assign(sessions_.begin(), sessions_.end());
    sessions_.clear();
  }

  for (const auto& session : copy) {
    if (session) {
      session->stop();
    }
  }
}

std::vector<std::shared_ptr<ClientSession>>
ClientManager::getAllSessions() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return {sessions_.begin(), sessions_.end()};
}

}  // namespace mcpe::protocol
