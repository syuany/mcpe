# MCPE 项目工程守则与架构记忆 (Project Guidelines & Architectural Memory)

本项目为基于 C++23 与 Asio 实现的高性能 Minecraft 1.12.2 (Protocol 340) 服务端，架构深度对齐工业级标杆 Cuberite。
所有协助开发此项目的 Agent 必须严格恪守本规则，严禁为了“快速出效果”而编写妥协性、过渡性的劣质代码（屎山）。

---

## 一、核心架构四层红线 (Strict 4-Layer Boundaries)

1. **Foundations (底层基础库 `src/common/`)**：
   - 包含 `ByteBuffer`, `VarInt`, 通用工具。
   - `ByteBuffer` 采用 Netty 风格零拷贝切片（`readSlice`）与 Copy-On-Write（写时复制隔离）。严禁在外部重复发明平行的 Reader/Writer 类（坚决杜绝 `ByteReader` 式冗余类）。
2. **Network (网络传输层 `src/network/`)**：
   - 包含 `TcpServer`, `TcpSession`, `PacketFraming`。
   - 纯粹负责 TCP 异步 I/O、连接生命周期与按字节定界拆包。**严禁引入任何 Minecraft 协议业务逻辑、状态机或 Packet ID**。
3. **Protocol (协议与会话层 `src/protocol/`)**：
   - 包含 `ClientSession`, `ClientManager`, `PacketRouter`, 各状态 Handler (`HandshakeHandler`, `StatusHandler`, `LoginHandler`, `PlayHandler`)。
   - `ClientManager` 统一管理所有在线会话并提供线程安全多播与统计；
   - 握手状态 `HANDSHAKING` 归属于 `HandshakeHandler`，严禁让 `StatusHandler` 越权处理握手。
4. **Domain (游戏领域层 `src/world/`, `src/entity/`)**：
   - 负责世界、区块、实体、游戏规则。与底层网络层完全解耦，严禁直接操作网络 socket。

---

## 二、防屎山硬性禁令 (Anti-Patterns Blacklist)

1. **严禁上帝类 (God Classes)**：
   - 类的职责必须单一（单一职责原则 SRP）。网络类不得处理业务，协议类不得直接处理物理区块数据。
2. **严禁“先写个玩具再重构” (No Quick-and-Dirty Hacks)**：
   - 骨架必须从第一天起按照 100% 工业级标准搭建。可以先空实现或记录日志，但模块边界、所有权归属绝不可走捷径。
3. **严禁裸写魔数 (Zero Magic Numbers)**：
   - 所有的包 ID、协议版本、维度、游戏模式、配置默认值，一律从 `PacketTypes.h` / `GameTypes.h` 强类型枚举与常量结构体中引用。
4. **严禁未保护的借用与野指针**：
   - 涉及数据切片与流式处理时，必须保障底层生命周期安全，严格遵循 `ByteBuffer` 的 COW 机制。

---

## 三、代码质量与交付门禁 (Definition of Done - DoD)

在提交任何代码前，必须保证：
1. **0 告警编译**：GCC 14 `-Wall -Wextra -Wpedantic` 零警告；
2. **100% 单元测试通过**：所有的 CTest 测试必须全部通过；
3. **真实环境回归闭环**：通过 Python 测试脚本（`test_ping.py`, `test_login.py`）验证端到端生命周期正常；
4. **代码整洁纯粹**：符合 Google C++ 风格指南，无死代码、无废弃头文件引用。
