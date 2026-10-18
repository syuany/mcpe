# MCPE 全景研发路线图与敏捷里程碑看板 (Milestone Roadmap)

> **项目定位**：基于 **C++23** 的高性能 Minecraft **轻量级集群大厅与跨服网关微服务引擎**（目标协议：**Minecraft 1.12.2 / Protocol 340**）。  
> **核心设计哲学**：**敏捷迭代（Agile Iteration）**、**严格完成定义（Strict DoD）**、**单测驱动（Test Driven）**、**工业级现代化架构（Modern C++23 / Asio / Multi-Instance Actor）**。

---

## 一、 总体研发里程碑全景 (Master Roadmap)

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ Sprint 0: 基础设施与叶子组件 (CMake、ByteBuffer、VarInt、现代化单测)              [100% 已完成] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 1: 网络基座与列表探测 (Asio 异步通信、粘包拆包、Server List Ping、MOTD)    [100% 已完成] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 2: 登录认证与区块下发 (Login 鉴权、离线 UUID、Join Game、5x5 紧凑区块下发)   [100% 已完成] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 3: 玩家移动同步与聊天系统 (0x0E/0x0F 移动消费、0x05/0x26 同屏广播、0x02 聊天) [100% 已完成] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 3.5: 离线地图烘焙、九宫格 AOI 裁剪、商业大厅机制与 C++20 网络协程化        [100% 已完成] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 4: 双层 INI 配置与多实例大厅动态分线并发引擎 (0x35 跨分线切换、弹性分线调度) [100% 已完成] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 5: BungeeCord 集群网关与交付 (IP穿透、0x18 跨服传送门、Docker 容器化、压测报告) [100% 已完成] │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 二、 阶段目标、交付物与完成定义 (Sprint Milestones & DoD)

### [x] Sprint 0: 基础设施与叶子组件 —— 【已完成】
* **核心交付物**：
  * `mcpe::ByteBuffer`：Netty 风格零拷贝切片读写缓冲与 COW 机制。
  * `mcpe::VarInt`：LEB128 变长整型编解码器，采用 `std::expected` 错误模型。
  * 完整的 GoogleTest 单元测试套件（100% 覆盖边界条件）。

---

### [x] Sprint 1: 网络基座与列表探测 (Server List Ping) —— 【已完成】
* **核心交付物**：
  * `mcpe_network`：基于 Standalone Asio 的异步 TCP 服务器，带串行化发送背压队列。
  * `PacketFraming`：VarInt 定界拆包器，支持零拷贝切片。
  * `HandshakeHandler` 与 `StatusHandler`：完整支持 1.12.2 官方客户端多人列表探测与 Ping/Pong 毫秒延迟回传。

---

### [x] Sprint 2: 登录认证与区块空间下发 (Login & Chunk Streaming) —— 【已完成】
* **核心交付物**：
  * `LoginHandler`：处理 `Login Start (0x00)`，生成离线 UUID 并下发 `Login Success (0x02)`。
  * `PlayHandler`：下发 `Join Game (0x23)`、`Spawn Position (0x46)`、`Player Position And Look (0x2F)` 并消费 `Teleport Confirm (0x00)`。
  * `mcpe_world` 空间底座：实现 `ChunkSection`（4096 方块调色板压缩编码）与 `0x20 Chunk Data` 序列化。

---

### [x] Sprint 3: 玩家移动同步与聊天系统 —— 【已完成】
* **核心交付物**：
  * 跨线程 MPSC 双缓冲队列 `EventQueue` 与 20 TPS `WorldTicker` 物理节拍器。
  * 实体生命周期同步（`0x2E Player List Item` -> `0x05 Spawn Player`、`0x26/0x27/0x28` 增量移动与 `0x4C` 8.0 格溢出降级、`0x32 Destroy Entities`）。
  * 聊天富文本多播与快捷指令分流系统（`ChatHandler`）。

---

### [x] Sprint 3.5: 离线地图烘焙、九宫格 AOI 裁剪与商业大厅机制 —— 【已完成】
* **核心交付物**：
  * `tools/map_baker` 离线地图烘焙工具链（剥离 `ZipArchive`、`NbtReader`、`AnvilLoader`、`SchematicLoader`），生成 `.mcpe_map` 实现线上 $<1\text{ms}$ 零解压冷启动。
  * `SpatialGrid` 基于 `ChunkPos` 双向哈希桶与九宫格邻居差异集的局部 AOI 视界裁剪。
  * 商业大厅机制（$11\times 11$ 视距区块下发、`0x47` 正午永昼锁定、`/fly` 权限防作弊、Fall Reset 虚空拉回状态闭环）。
  * 网络层 C++20 无栈协程化（`asio::awaitable`）、`ByteBuffer::retainedSlice()` 真零拷贝多播与 `PacketBuilder` 统一封包。

---

### [x] Sprint 4: 双层 INI 配置体系与多实例大厅并发分线引擎 —— 【已完成】
* **核心交付物**：
  * **双层 INI 配置持久化（`IniConfig`）**：`src/settings.ini` 管理全局网络与分线配额（支持管理员 `/setmaxplayers` 实时写回），`assets/maps/<map>.ini` 管理出生点与跌落拉回高度（支持 `/setspawn` 实时写回）。
  * **多实例并发 Actor 引擎（`src/lobby/`）**：`LobbyInstance` 封装单个大厅分线的独立 `EventQueue` + `EntityManager` + `WorldTicker`（20 TPS 专属线程），静态地形 `World` 全局只读共享，单分线增量内存开销 $<16\text{KB}$。
  * **大厅弹性分线与全服满员拦截（`LobbyManager` & `LoginHandler`）**：全服超限在 `LoginHandler` 下发 `0x00 Disconnect`；单大厅满员自动裂变 `lobby-2..N` 分线，支持跨分线原子切换（`/lobby [id]` 与 `/lobbies`）。
  * **业务架构边界聚焦**：严守极简微服务边界，不引入原版红石、压力板、活塞等物理机制，主工程全量聚焦高并发集群大厅与跨服路由网关。

---

### [x] Sprint 5: BungeeCord 集群网关与产品化交付 —— 【已完成】
* **核心交付物**：
  * **集群网关与协议穿透**：原生兼容 BungeeCord / Velocity IP Forwarding 协议穿透与正版 UUID/Skin 属性提取，集成 Bungee-Guard 防伪机制。
  * **跨服传送门系统**：基于 `0x18 Plugin Message (Channel="BungeeCord")` 的切服信令，大厅传送门空间 AABB 检测、防抖冷却与 `/server` 路由。
  * **生产级容器化打包**：多阶段构建极简 `Dockerfile`（$\le 30\text{MB}$ 镜像、无头冷启动、UID 1000 非 root 安全运行）与 `docker-compose.yml` 集群编排。
  * **千人无头 Bot 压测与基准报告**：`scripts/benchmark_swarm.py` 驱动 1,000 Bots 100% 就绪实测（0 失败，1.74GB 吞吐，常驻内存 127MB），输出量化对比报告 `doc/benchmark_report_sprint5.md`。
