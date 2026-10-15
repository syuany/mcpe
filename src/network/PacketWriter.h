#ifndef PACKETWRITER_H
#define PACKETWRITER_H

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

#include "common/ByteBuffer.h"

namespace mcpe::network {

class TcpSession;

/**
 * @brief 无会话绑定的数据包构建器（用于构建待多播广播或跨模块传递的独立协议包）
 *
 * 构造时自动写入 VarInt 格式的 Packet ID；调用 build() 时自动通过 Cheap Prepend
 * 原位前插包长 VarInt 前缀并移出完整 ByteBuffer，彻底消除手写 prependVarInt 样板代码。
 */
class PacketBuilder {
 public:
  explicit PacketBuilder(int32_t packet_id, size_t initial_size = ByteBuffer::kInitialSize)
      : packet_id_(packet_id), buffer_(initial_size) {
    buffer_.appendVarInt(packet_id_);
  }

  PacketBuilder(const PacketBuilder&) = delete;
  PacketBuilder& operator=(const PacketBuilder&) = delete;
  PacketBuilder(PacketBuilder&&) noexcept = default;
  PacketBuilder& operator=(PacketBuilder&&) noexcept = default;
  ~PacketBuilder() = default;

  template <typename T>
  void
  writeBe(T value) {
    buffer_.writeBe<T>(value);
  }

  void
  appendVarInt(int32_t value) {
    buffer_.appendVarInt(value);
  }

  void
  appendVarLong(int64_t value) {
    buffer_.appendVarLong(value);
  }

  void
  appendString(std::string_view str) {
    buffer_.appendString(str);
  }

  void
  appendByte(uint8_t byte) {
    buffer_.appendByte(byte);
  }

  void
  append(const void* data, size_t len) {
    buffer_.append(data, len);
  }

  void
  append(std::string_view str) {
    buffer_.append(str);
  }

  [[nodiscard]] int32_t
  packetId() const noexcept {
    return packet_id_;
  }

  /**
   * @brief 完成封包：在头部原位前插包长 VarInt 并移出构建完毕的 ByteBuffer
   */
  [[nodiscard]] ByteBuffer
  build() noexcept {
    if (!is_built_) {
      is_built_ = true;
      buffer_.prependVarInt(static_cast<int32_t>(buffer_.readableBytes()));
    }
    return std::move(buffer_);
  }

 private:
  int32_t packet_id_{0};
  ByteBuffer buffer_;
  bool is_built_{false};
};

/**
 * @brief 基于 RAII 机制的数据包自动打包与发送器（借鉴 Cuberite cPacketizer 架构设计）
 *
 * 泛型支持 TcpSession 与 ClientSession 等任意具备 sendPacket(ByteBuffer) 契约的会话对象。
 * 1. 构造即写入 Packet ID：自动将 VarInt 类型的 Packet ID 写入底层缓冲区
 * 2. RAII 自动封包与下发：当局部 PacketWriter 对象离开作用域析构时，
 *    自动通过 Cheap Prepend 原位前插包长 VarInt 前缀，并投递至 Session 发送队列（零内存拷贝）
 * 3. 杜绝遗漏与错发：消除开发者手动计算包长、前插 VarInt 以及显式调用 sendPacket 的心智负担
 * 4. 异常安全与取消支持：支持 cancel() 在逻辑分支中止时不发送残包
 */
template <typename SessionType = TcpSession>
class PacketWriter {
 public:
  explicit PacketWriter(std::shared_ptr<SessionType> session, int32_t packet_id)
      : session_(session.get()), session_shared_(std::move(session)), packet_id_(packet_id) {
    init();
  }

  explicit PacketWriter(SessionType& session, int32_t packet_id)
      : session_(&session), packet_id_(packet_id) {
    init();
  }

  ~PacketWriter() { flush(); }

  // Move-only 语义
  PacketWriter(const PacketWriter&) = delete;
  PacketWriter& operator=(const PacketWriter&) = delete;

  PacketWriter(PacketWriter&& other) noexcept
      : session_(other.session_),
        session_shared_(std::move(other.session_shared_)),
        packet_id_(other.packet_id_),
        buffer_(std::move(other.buffer_)),
        is_active_(other.is_active_) {
    other.is_active_ = false;
    other.session_ = nullptr;
  }

  PacketWriter&
  operator=(PacketWriter&& other) noexcept {
    if (this != &other) {
      flush();
      session_ = other.session_;
      session_shared_ = std::move(other.session_shared_);
      packet_id_ = other.packet_id_;
      buffer_ = std::move(other.buffer_);
      is_active_ = other.is_active_;
      other.is_active_ = false;
      other.session_ = nullptr;
    }
    return *this;
  }

  /// 显式放弃当前数据包发送（析构时不再触发 sendPacket）
  void
  cancel() noexcept {
    is_active_ = false;
  }

  /// 是否仍处于待发送激活状态
  bool
  isActive() const noexcept {
    return is_active_;
  }

  /// 获取底层 ByteBuffer 引用
  ByteBuffer&
  buffer() noexcept {
    return buffer_;
  }
  const ByteBuffer&
  buffer() const noexcept {
    return buffer_;
  }

  template <typename T>
  void
  writeBe(T value) {
    buffer_.writeBe<T>(value);
  }

  void
  appendVarInt(int32_t value) {
    buffer_.appendVarInt(value);
  }

  void
  appendVarLong(int64_t value) {
    buffer_.appendVarLong(value);
  }

  void
  appendString(std::string_view str) {
    buffer_.appendString(str);
  }

  void
  appendByte(uint8_t byte) {
    buffer_.appendByte(byte);
  }

  void
  append(const void* data, size_t len) {
    buffer_.append(data, len);
  }
  void
  append(std::string_view str) {
    buffer_.append(str);
  }

  int32_t
  packetId() const noexcept {
    return packet_id_;
  }

  /// 手动提前完成封包下发
  void
  flush() {
    if (is_active_ && session_ != nullptr && buffer_.readableBytes() > 0) {
      is_built_and_sent();
    }
  }

 private:
  void
  init() {
    buffer_.appendVarInt(packet_id_);
    is_active_ = true;
  }

  void
  is_built_and_sent() {
    is_active_ = false;
    buffer_.prependVarInt(buffer_.readableBytes());
    session_->sendPacket(std::move(buffer_));
  }

 private:
  SessionType* session_{nullptr};
  std::shared_ptr<SessionType> session_shared_{nullptr};
  int32_t packet_id_{0};
  ByteBuffer buffer_;
  bool is_active_{false};
};

template <typename SessionType>
PacketWriter(std::shared_ptr<SessionType>, int32_t) -> PacketWriter<SessionType>;

template <typename SessionType>
PacketWriter(SessionType&, int32_t) -> PacketWriter<SessionType>;

template <typename SessionType>
using BasicPacketWriter = PacketWriter<SessionType>;

}  // namespace mcpe::network

#endif  // PACKETWRITER_H
