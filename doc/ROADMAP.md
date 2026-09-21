# MCPE 全景研发路线图与敏捷里程碑看板 (Milestone Roadmap)

> **项目定位**：基于 **C++23** 的现代化高性能 Minecraft 协议服务器与空间同步引擎（目标协议：**Minecraft Java Edition 1.12.2 / Protocol 340**），并搭载基于 **Go + gRPC** 的分布式游戏数据中心微服务（**EnderVault**）。  
> **核心设计哲学**：**小步快跑（Agile Iteration）**、**严格完成定义（Strict DoD）**、**单测与性能驱动（Test & Perf Driven）**、**工业级代码规范（Modern C++23）**。  
> **总览速查**：详细的 WBS 任务细化拆解见 [WBS 任务总看板](file:///home/ubu/dev/my/mcpe/docs/sprints/tasks_breakdown.md)，各阶段独立实战指引见 [sprints/ 阶段速查目录](file:///home/ubu/dev/my/mcpe/docs/sprints/00_overview.md)。

---

## 一、 总体研发里程碑全景 (Master Roadmap)

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ Sprint 0: 基础设施与叶子组件 (CMake、ByteBuffer、VarInt、现代化单测)              [100% 已完成] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 1: 网络基座与列表探测 (Asio 异步通信、粘包拆包、Server List Ping、MOTD)    [ 90% 进行中] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 2: 会话生命周期与虚空进服 (Login 鉴权、离线 UUID、Join Game、出生点、KeepAlive)   [ 0% 待启动] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 3: 区块模型与超平坦地形 (ChunkSection 紧凑内存、地形生成、0x20 区块包、踏下方块)    [ 0% 待启动] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 4: 架构解耦与多玩家同步 (MPSC 任务队列、20 TPS 节拍器、九宫格 AOI、同屏移动/挖掘)  [ 0% 待启动] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Sprint 5: 压测机器人与性能调优 (千人 Headless Bot、perf 火焰图、定长内存池、Sanitizers)   [ 0% 待启动] │
├────────────────────────────────────────────────────────────────────────────────────────┤
│ Phase 2:  EnderVault 分布式数据中心 (Go+gRPC 微服务、Redis 7.0 缓存、MySQL 8.0 持久化) [ 规划中 ] │
└────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 二、 阶段目标、交付物与完成定义 (Sprint Milestones & DoD)

### [x] Sprint 0: 基础设施与叶子组件 (工期: 2~3 天) —— 【已完成】
* **核心目标**：搭建现代 CMake 构建脚手架，实现协议底座核心的无异常二进制缓冲区与变长整型算法，以 100% 单元测试为基准。
* **核心交付物**：
  * [CMakeLists.txt](file:///home/ubu/dev/my/mcpe/CMakeLists.txt)：模块化目标拆分（`mcpe_common` 静态库 + 单元测试目标），支持 C++23。
  * [`mcpe::ByteBuffer`](file:///home/ubu/dev/my/mcpe/include/mcpe/common/ByteBuffer.h)：基于双指针与 `std::vector<uint8_t>` 的动态缓冲区，提供基于大端序（Big-Endian）的读写接口与 `std::span` 视图。
  * [`mcpe::VarInt`](file:///home/ubu/dev/my/mcpe/include/mcpe/common/VarInt.h)：LEB128 变长整型的高性能编解码器，采用 `std::expected` 错误传递范式。
  * [单测套件](file:///home/ubu/dev/my/mcpe/tests/)：`test_bytebuffer` 与 `test_varint` 覆盖所有边界值（0, 1, 127, 128, 255, 2147483647, -1 等）。
* **技术文档**：
  * [Sprint 0 看板指南](file:///home/ubu/dev/my/mcpe/docs/sprints/01_sprint0_infra.md)
  * [ByteBuffer 深度设计与 Muduo 对比专题](file:///home/ubu/dev/my/mcpe/docs/guides/bytebuffer_design_and_muduo.md)

---

### [~] Sprint 1: 网络基座与列表探测 (工期: 3~4 天) —— 【进行中，完成度 90%】
* **核心目标**：基于 Standalone Asio 构建异步非阻塞 TCP 网络骨架，彻底消除粘包与半包，实现 Minecraft 状态机路由与客户端列表探测（Server List Ping / MOTD 展示）。
* **核心交付物**：
  * [TcpServer 与 TcpSession](file:///home/ubu/dev/my/mcpe/include/mcpe/network/TcpServer.h)：会话生命周期管理（`shared_from_this` 延长生命周期）、Asio Strand 保护的无锁化安全发送队列。
  * [PacketFraming](file:///home/ubu/dev/my/mcpe/include/mcpe/network/PacketFraming.h)：定界拆包器，支持流式分帧与超限防御（最大包长度 2MB）。
  * [ConnectionState 与 PacketRouter](file:///home/ubu/dev/my/mcpe/include/mcpe/protocol/ConnectionState.h)：连接生命周期状态机与动态包路由分发机制。
  * [StatusHandler](file:///home/ubu/dev/my/mcpe/include/mcpe/protocol/StatusHandler.h)：响应客户端的 `Status Request (0x00)` 并下发 MOTD JSON，响应 `Ping (0x01)` 并原样回传时间戳 `Pong (0x01)`。
* **当前进度追踪**：
  * [x] TASK-1.1: 真实客户端协议抓包与基准分析 (`docs/protocol/packets_hex_dump.md`)
  * [x] TASK-1.2: Asio TCP 服务端脚手架与连接生命周期
  * [x] TASK-1.3: 线程安全写队列与发送背压控制
  * [x] TASK-1.4: 定界拆包器 PacketFraming 与粘包半包消除
  * [x] TASK-1.5: 协议状态机框架搭建 (ConnectionState 与 PacketRouter)
  * [x] TASK-1.6: 握手包解码与协议版本协商 (Handshake Packet 0x00)
  * [x] TASK-1.7: 状态查询响应与 MOTD 协议构造 (Status Request/Response 0x00)
  * [x] TASK-1.8: 延迟探测与双向保活链路闭环 (Ping/Pong 0x01)
  * [ ] TASK-1.9: Sprint 1 联调、真实客户端接入与质量收敛
* **技术文档**：
  * [Sprint 1 敏捷开发指引](file:///home/ubu/dev/my/mcpe/docs/sprints/02_sprint1_network_ping.md)
  * [Sprint 1 保姆级实战开发教程](file:///home/ubu/dev/my/mcpe/docs/tutorial/sprint1_network_ping.md)
  * [Asio 核心原语快速入门](file:///home/ubu/dev/my/mcpe/docs/guides/asio_primer_guide.md)
  * [TCP 拆包与高频面试核心 QA](file:///home/ubu/dev/my/mcpe/docs/guides/tcp_framing_and_interview_qa.md)

---

### [ ] Sprint 2: 会话生命周期与虚空进服 (工期: 4~5 天) —— 【待启动】
* **核心目标**：打通客户端登录流程，生成离线 UUID，下发登录成功、核心加入游戏配置包与出生点坐标，实现客户端画面渲染并站在虚空平台上。
* **核心交付物**：
  * `LoginHandler`：处理 `Login Start (0x00)`，实现离线模式 UUID（MD5(OfflinePlayer:name)）计算，下发 `Login Success (0x02)`。
  * `Join Game (0x23)`：下发世界维度（0 为主世界）、游戏模式（1 创造模式）、实体 EID 等关键参数。
  * `Player Position And Look (0x2F)`：下发玩家出生坐标（X=0.0, Y=64.0, Z=0.0）与 Teleport ID，并在消费客户端回传的 `Teleport Confirm (0x00)` 时完成闭环。
  * `KeepAlive`：服务端周期性（如每 15 秒）下发 `Keep Alive (0x1F)`，校验客户端回传 `Keep Alive (0x0B)`，超时 30 秒踢出玩家。
* **技术文档**：
  * [Sprint 2 开发规划](file:///home/ubu/dev/my/mcpe/docs/sprints/03_sprint2_login_spawn.md)

---

### [ ] Sprint 3: 区块模型与超平坦地形 (工期: 4~5 天) —— 【待启动】
* **核心目标**：构建 16x16x16 的 ChunkSection 紧凑内存模型，实现超平坦世界生成算法与 `0x20` 区块流式网络下发，让玩家站在实体方块上。
* **核心交付物**：
  * `ChunkSection` 与 `ChunkColumn`：4096 方块的紧凑内存排布，实现 Palette（调色板）映射与位打包算法。
  * `FlatWorldGenerator`：生成基岩（y=0）、泥土（y=1~3）、草方块（y=4）的经典超平坦结构。
  * `Chunk Data (0x20)`：构造完整的压缩区块网络包并流式发送到客户端。
* **技术文档**：
  * [Sprint 3 开发规划](file:///home/ubu/dev/my/mcpe/docs/sprints/04_sprint3_chunk_world.md)

---

### [ ] Sprint 4: 架构解耦与多玩家同步 (工期: 5~6 天) —— 【待启动】
* **核心目标**：解耦网络 I/O 与业务逻辑，引入基于 MPSC 任务队列的 20 TPS 物理节拍器，基于九宫格 AOI 空间算法实现同屏玩家移动与挖掘同步。
* **核心交付物**：
  * `WorldTicker`：主游戏线程驱动的 20 TPS（50ms/tick）恒定物理更新循环与定时器漂移补偿。
  * `MPSC TaskQueue`：无锁/细粒度队列，网络 I/O 线程将客户端操作封装为任务投递到世界线程，彻底消除世界状态并发竞争。
  * `GridAOI`：九宫格空间索引，维护玩家进入（Enter）、离开（Leave）、移动（Move）事件。
  * 玩家同步包：`Spawn Player (0x05)`、`Destroy Entities (0x32)`、`Entity Relative Move (0x26)`。
* **技术文档**：
  * [Sprint 4 开发规划](file:///home/ubu/dev/my/mcpe/docs/sprints/05_sprint4_concurrency_aoi.md)

---

### [ ] Sprint 5: 压测机器人与性能调优 (工期: 4~5 天) —— 【待启动】
* **核心目标**：开发 Headless 压测机器人框架，开展千人并发漫游测试，运用 Linux `perf` 绘制火焰图，消除热点瓶颈与内存碎片。
* **核心交付物**：
  * `mcpe_bot`：基于 C++/Python 编写的高性能无头压测客户端，模拟真实玩家登录、移动与发包。
  * `perf` 火焰图分析与瓶颈优化：针对 VarInt 编解码、JSON 序列化与内存分配进行极致性能调优。
  * 定长内存池与零拷贝：优化拆包切片，消除动态小内存分配。
  * 内存与线程安全净化：通过 ASan / TSan / UBSan 验证，零内存泄漏、零未定义行为、零竞态。
* **技术文档**：
  * [Sprint 5 开发规划](file:///home/ubu/dev/my/mcpe/docs/sprints/06_sprint5_perf_tuning.md)
  * [企业级单测与质量保障标准](file:///home/ubu/dev/my/mcpe/docs/guides/enterprise_testing_and_dod_guide.md)

---

### [ ] Phase 2: EnderVault 分布式数据中心微服务 (工期: 2.5 周) —— 【长期规划】
* **核心目标**：作为 `mcpe` Monorepo 大仓下的子服务（`services/endervault/`），基于 **Go + gRPC + Redis + MySQL** 构建分布式玩家资产中心与跨服同步微服务。
* **核心交付物**：
  * `proto/endervault.proto`：C++ 与 Go 共享的 gRPC 契约，提供玩家鉴权、资产查询、道具存取等 RPC 接口。
  * Redis 7.0 分布式缓存：维护玩家 Session、背包数据、基于 Redlock/SetNX 的防并发刷道具分布式锁。
  * MySQL 8.0 规范化持久层：支持 ACID 资产流水与离线结算。
  * 缓存一致性保障：Cache-Aside 结合延时双删、异步刷盘与防穿透/雪崩机制。
* **技术文档**：
  * [EnderVault 架构设计详述](file:///home/ubu/dev/my/mcpe/docs/services/EnderVault.md)
  * [经典公开课 Lab 评估与项目规划](file:///home/ubu/dev/my/mcpe/docs/course_labs_evaluation_and_plan.md)

---

## 三、 核心文档导航索引

| 序号 | 文档名称 | 定位与核心内容 | 状态 |
| :---: | :--- | :--- | :---: |
| **00** | [ROADMAP.md](file:///home/ubu/dev/my/mcpe/docs/ROADMAP.md) | **全景研发路线图与敏捷里程碑看板**（本文档） | `已发布` |
| **01** | [01_PRD_需求与业务规约.md](file:///home/ubu/dev/my/mcpe/docs/01_PRD_需求与业务规约.md) | **产品需求规格说明书与业务合规边界** | `已发布` |
| **02** | [02_技术选型与系统架构设计.md](file:///home/ubu/dev/my/mcpe/docs/02_技术选型与系统架构设计.md) | **分层架构拓扑、现代 C++23/Asio/Go 技术栈论证** | `已发布` |
| **03** | [03_网络通信与通信协议规范.md](file:///home/ubu/dev/my/mcpe/docs/03_网络通信与通信协议规范.md) | **Minecraft 1.12.2 (340) 协议帧、VarInt 与数据包物理详表** | `已发布` |
| **04** | [04_数据存储与缓存架构设计.md](file:///home/ubu/dev/my/mcpe/docs/04_数据存储与缓存架构设计.md) | **Anvil 区块存储、紧凑 Section、EnderVault Redis/MySQL 多级缓存** | `已发布` |
| **05** | [05_房间状态机与状态同步设计.md](file:///home/ubu/dev/my/mcpe/docs/05_房间状态机与状态同步设计.md) | **连接生命周期状态机、20 TPS 物理主循环、九宫格 AOI 同步** | `已发布` |
| **06** | [06_运维热更与监控压测方案.md](file:///home/ubu/dev/my/mcpe/docs/06_运维热更与监控压测方案.md) | **Docker/Compose 编排、SIGHUP 热更、Prometheus 监控与千人压测** | `已发布` |
| **07** | [07_项目学习资源与技术图谱.md](file:///home/ubu/dev/my/mcpe/docs/07_项目学习资源与技术图谱.md) | **C++23/网络/游戏全栈技能树、经典书籍文献与面试深度 QA 索引** | `已发布` |
| **附** | [QA.md](file:///home/ubu/dev/my/mcpe/docs/QA.md) | **全能面试与技术疑难解答宝典 (250KB+)** | `持续更新` |
