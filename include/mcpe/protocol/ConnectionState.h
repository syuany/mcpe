#ifndef CONNECTIONSTATE_H
#define CONNECTIONSTATE_H

#include <cstdint>
#include <string_view>
namespace mcpe::protocol {

enum class ConnectionState : uint8_t { HANDSHAKING = 0, STATUS = 1, LOGIN = 2, PLAY = 3 };

constexpr std::string_view
toString(ConnectionState state) noexcept {
  switch (state) {
    case ConnectionState::HANDSHAKING:
      return "HANDSHAKING";
    case ConnectionState::STATUS:
      return "STATUS";
    case ConnectionState::LOGIN:
      return "LOGIN";
    case ConnectionState::PLAY:
      return "PLAY";
  }
  return "UNKNOWN";
}

}  // namespace mcpe::protocol

#endif  // CONNECTIONSTATE_H