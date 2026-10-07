# MCPE 项目工程守则与架构记忆 (Project Guidelines & Architectural Memory)

本项目为基于 C++23 与 Asio 实现的高性能 Minecraft 1.12.2 (Protocol 340) **轻量级集群大厅与多地图跑酷微服务引擎 (Lobby & Parkour Microservice Engine)**。
架构深度对齐现代 C++ 最佳实践与分布式游戏微服务标准，兼顾**单机多房间小游戏**与**商业群组服即插即用网关大厅**双重定位。
所有协助开发此项目的 Agent 必须严格恪守本规则，严禁为了“快速出效果”而编写妥协性、过渡性的劣质代码（屎山）。

---

## 一、产品定位与核心功能边界 (Product Boundaries)

1. **核心产品形态**：
   - **极致轻量大厅/挂机微服务 (Lobby/Limbo)**：内存占用 $\le 30\text{MB}$，冷启动 $\le 50\text{ms}$，提供大厅漫游、聊天多播与低资源挂机。
   - **多地图跑酷引擎 (Multi-Map Parkour Engine)**：内置 Schematic 建筑蓝图与紧凑区块加载器，单进程内通过 `0x35 Respawn` 实现无缝切世界/选图，提供终点结算、掉落重置与排行榜。
   - **即插即用集群网关 (Cluster Gateway)**：原生兼容 BungeeCord / Velocity IP Forwarding 协议穿透；支持 `0x18 Plugin Message (BungeeCord:Connect)` 传送门跨服路由，可无缝热插拔接入主流商业群组网络。
2. **明确剪裁的非目标 (Explicit Non-Goals)**：
   - **不做原版生存体系**：不实现复杂的原版生物 AI 寻路、红石电路模拟、方块破坏物理掉落物与容器合成配方，避免陷入无边界协议与游戏机制泥潭。

---

## 二、核心架构四层红线 (Strict 4-Layer Boundaries)

1. **Foundations (底层基础库 `src/common/`)**：
   - 包含 `ByteBuffer`, `VarInt`, 通用工具。
   - `ByteBuffer` 采用 Netty 风格零拷贝切片（`readSlice`）与 Copy-On-Write（写时复制隔离）。严禁在外部重复发明平行的 Reader/Writer 类（坚决杜绝 `ByteReader` 式冗余类）。
2. **Network (网络传输层 `src/network/`)**：
   - 包含 `TcpServer`, `TcpSession`, `PacketFraming`。
   - 纯粹负责 TCP 异步 I/O、连接生命周期与按字节定界拆包。**严禁引入任何 Minecraft 协议业务逻辑、状态机或 Packet ID**。
3. **Protocol (协议与会话层 `src/protocol/`)**：
   - 包含 `ClientSession`, `ClientManager`, `PacketRouter`, 各状态 Handler (`HandshakeHandler`, `StatusHandler`, `LoginHandler`, `PlayHandler`)。
   - `ClientManager` 统一管理所有在线会话并提供线程安全多播与统计；
   - 握手状态 `HANDSHAKING` 归属于 `HandshakeHandler`，支持 BungeeCord IP 穿透字符串解析；
   - 严禁让 `StatusHandler` 越权处理握手。
4. **Domain & Game (游戏领域与房间层 `src/world/`, `src/game/`)**：
   - 负责世界、区块加载（Schematic）、跑酷房间生命周期（Room FSM）、玩家位姿与跨服传送门。与底层网络层完全解耦，严禁直接操作裸 socket。

---

## 三、防屎山硬性禁令 (Anti-Patterns Blacklist)

1. **严禁上帝类 (God Classes)**：
   - 类的职责必须单一（单一职责原则 SRP）。网络类不得处理业务，协议类不得直接管理游戏房间逻辑。
2. **严禁“先写个玩具再重构” (No Quick-and-Dirty Hacks)**：
   - 骨架必须从第一天起按照 100% 工业级标准搭建。可以先空实现或记录日志，但模块边界、所有权归属绝不可走捷径。
3. **严禁裸写魔数 (Zero Magic Numbers)**：
   - 所有的包 ID、协议版本、维度、游戏模式、配置默认值，一律从 `PacketTypes.h` / `GameTypes.h` 强类型枚举与常量结构体中引用。
4. **严禁未保护的借用与野指针**：
   - 涉及数据切片与流式处理时，必须保障底层生命周期安全，严格遵循 `ByteBuffer` 的 COW 机制。

---

## 四、代码质量与交付门禁 (Definition of Done - DoD)

在提交任何代码前，必须保证：
1. **0 告警编译**：GCC 14 `-Wall -Wextra -Wpedantic` 零警告；
2. **100% 单元测试通过**：所有的 CTest 测试必须全部通过；
3. **真实环境回归闭环**：通过 Python 测试脚本验证端到端生命周期正常，并使用真实 Minecraft 1.12.2 客户端连入验证；
4. **代码整洁纯粹**：符合 Google C++ 风格指南，无死代码、无废弃头文件引用。

---

## 五、Git 提交信息规范 (Google Engineering Practices)

所有 Git Commit Message 必须严格遵循 **Google 工程开发规范 (Google Engineering Practices)** 与 **约定式提交 (Conventional Commits)**，且**必须使用 100% 纯英文**，严禁中英混杂：
1. **格式结构**：
   ```text
   <type>(<scope>): <subject in imperative mood, max 72 chars>

   [optional body: explain motivation (why) and changes (what)]
   ```
2. **Subject 规范**：
   - 使用祈使句式（Imperative mood，如 `add`, `update`, `fix`, `refactor`, `pivot`，不使用过去式 `added`/`fixed`）；
   - 长度不超过 72 个字符，句末不加句号；
   - 常见 Type：`feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`, `chore`。
3. **Body 规范**：
   - 与首行空一行分隔；
   - 详细阐述改动的上下文动机（Why）与具体修改内容（What），按 72 列折行。
