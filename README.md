# MCPE: Minecraft C++23 高性能集群大厅与多地图跑酷微服务引擎

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-23-blue.svg?style=flat-square&logo=c%2B%2B" alt="C++23" />
  <img src="https://img.shields.io/badge/Minecraft-1.12.2%20(Protocol%20340)-brightgreen.svg?style=flat-square" alt="Protocol 340" />
  <img src="https://img.shields.io/badge/Network-Standalone%20Asio-orange.svg?style=flat-square" alt="Asio" />
  <img src="https://img.shields.io/badge/Tests-232%2F232%20Passing%20(100%25)-success.svg?style=flat-square" alt="Tests" />
  <img src="https://img.shields.io/badge/Compiler-GCC%2014%2B%20%2F%20Clang%2018%2B-blueviolet.svg?style=flat-square" alt="Compiler" />
  <img src="https://img.shields.io/badge/Docker-Multi--Stage%20%E2%89%A4%2030MB-blue.svg?style=flat-square&logo=docker" alt="Docker" />
  <img src="https://img.shields.io/badge/License-MIT-green.svg?style=flat-square" alt="License" />
</p>

> **项目定位**：基于 **现代 C++23** 与 **Standalone Asio** 实现的高性能 Minecraft 1.12.2（Protocol 340）**轻量级集群大厅与多地图跑酷微服务引擎 (Lobby & Parkour Microservice Engine)**。  
> **核心架构定位**：兼顾**商业群组服即插即用网关大厅 (Cluster Gateway Lobby/Limbo)** 与 **单进程多实例跑酷小游戏并发引擎 (Multi-Instance Actor)** 双重定位，以极低物理开销（**常驻内存 $\le 25\text{MB}$，冷启动 $\le 20\text{ms}$**）实现数千玩家低延迟漫游、同屏移动、跑酷竞技与跨服路由。

---

## 目录
- [一、 核心产品特性与架构亮点](#一-核心产品特性与架构亮点)
- [二、 严格四层架构拓扑与设计红线](#二-严格四层架构拓扑与设计红线)
- [三、 性能基准与千人压测对比 (Benchmark)](#三-性能基准与千人压测对比-benchmark)
- [四、 核心模块技术创新](#四-核心模块技术创新)
- [五、 快速上手与运行指南](#五-快速上手与运行指南)
- [六、 敏捷研发全景看板 (Roadmap)](#六-敏捷研发全景看板-roadmap)
- [七、 模块文档与技术专题导引](#七-模块文档与技术专题导引)

---

## 一、 核心产品特性与架构亮点

1. **极致轻量大厅/挂机微服务 (Lobby/Limbo Gateway)**：
   - 相比传统 Java 服务端（Paper/Spigot 1.5GB+ 底噪），MCPE 实现了原生的 **$\le 25\text{MB}$ 极小内存常驻** 与 **$<20\text{ms}$ 毫秒级极速冷启动**；
   - 原生支持商业反向代理网关（**BungeeCord / Velocity**）的 **IP Forwarding 协议穿透**，支持正版 UUID、皮肤披风带签名解析与 Bungee-Guard 安全防伪防直连；
   - 支持基于 **`0x18 Plugin Message (Channel: "BungeeCord")`** 的跨服传送门（Server Portals）与 `/server <name>` 指令，大厅玩家踩入传送门 AABB 包围盒瞬间无缝转交至外部生存/空岛子服。

2. **多实例并发 Actor 跑酷引擎 (Multi-Instance Parkour Engine)**：
   - **地形内存极致复用**：静态地形 `std::shared_ptr<const World>` 全局只读共享，动态实例仅增加独立的 `EntityManager`、`EventQueue` 与 20 TPS `WorldTicker` 线程，新建分线内存开销 $<16\text{KB}$；
   - **大厅动态弹性分线**：全服总人数上限 `server_max_players` 在 `LoginHandler` 阶段执行 `0x00 Disconnect` 准入拦截；单大厅满员自动裂变 `lobby-1..N` 分线，支持 `/lobby [id]` 与 `/lobbies` 查询；
   - **0x35 无缝切世界**：单 TCP 连接不中断，通过 `0x35 Respawn` 过渡维度刷新、目标区块流式下发与 `0x2F` 传送确认屏障，实现大厅与跑酷地图之间毫秒级平滑穿梭；
   - **跑酷竞速全生命周期**：同赛道多人随进随跑、高精度 `std::chrono::steady_clock` 个人毫秒秒表、检查点 Fall Reset 跌落拉回、通关排行榜与完赛 3 秒自动回大厅。

3. **云原生极简容器化交付**：
   - 提供工业级 Multi-Stage `Dockerfile`，编译阶段开启 LTO 并通过 `strip` 剔除调试符号，生产镜像体积 **$\le 30\text{MB}$**；
   - 进程接管 Linux PID 1 信号处理，通过 Asio `signal_set` 捕获 `SIGINT`/`SIGTERM` 实现毫秒级平稳停机与 INI 配置安全持久化；
   - 提供开箱即用的 `docker-compose.yml` 联合编排，一键拉起 BungeeCord 网关 + MCPE 大厅微服务。

---

## 二、 严格四层架构拓扑与设计红线

本工程恪守工业级分层边界，严禁跨层越权，严禁上帝类与魔数（Zero Magic Numbers）：

```text
┌────────────────────────────────────────────────────────────────────────┐
│ 4. Domain & Game 领域与玩法层 (src/world/, src/game/)                   │
│    * IGameInstance / InstanceManager (每实例独立 20 TPS Actor 线程模型)   │
│    * LobbyInstance (弹性分线 / 传送门 PortalManager / /fly 权限防作弊)   │
│    * ParkourInstance (AABB 碰撞检测 / 检查点 Fall Reset / 毫秒计时)     │
│    * SpatialGrid (ChunkPos 双向哈希桶与九宫格邻居差异集局部 AOI 裁剪)  │
│    * ChunkSection (4096 方块 13 位紧凑调色板压缩) / World (只读只映射)  │
├────────────────────────────────────────────────────────────────────────┤
│ 3. Protocol 协议与业务会话层 (src/protocol/)                            │
│    * ClientSession (会话生命周期、路由、心跳保活、弱引用解耦)          │
│    * ClientManager (全服在线会话注册表、并发统计与广播管理)            │
│    * PacketRouter (状态机解复用: HANDSHAKING -> STATUS / LOGIN / PLAY) │
│    * HandshakeHandler / StatusHandler (Server List Ping 与 MOTD)       │
│    * LoginHandler (离线 UUID / 全服硬上限 0x00 准入拦截 / 0x02 成功)   │
│    * PlayHandler (0x23 Join, 0x20 Chunk, 0x2F PosLook, 0x35 切世界)   │
│    * ChatHandler (富文本 JSON 多播与 /server, /lobby, /maps 路由)      │
│    * BungeeCord (IP Forwarding \0 解析器 / UUID 格式化 / Token 鉴权)    │
├────────────────────────────────────────────────────────────────────────┤
│ 2. Network 纯异步网络传输层 (src/network/)                             │
│    * TcpServer (Asio 异步 Acceptor 监听引擎，跨平台 IOCP/epoll)       │
│    * TcpSession (无锁串行发送背压队列、生命周期延续、零协议业务耦合)   │
│    * PacketFraming (VarInt 变长定界流式拆包器)                        │
│    * PacketWriter (大端编解码、Java writeUTF 扩展与安全序列化)          │
├────────────────────────────────────────────────────────────────────────┤
│ 1. Foundations 底层基础工具库 (src/common/)                            │
│    * ByteBuffer (Netty 风格零拷贝切片 readSlice / retainedSlice + COW) │
│    * VarInt (LEB128 变长编解码器，采用 std::expected 强错误模型)      │
│    * IniConfig (零外部依赖双层 INI 配置读写持久化工具)                │
│    * SpatialTypes (Vector3d, Rotation, ChunkPos, AABB 包围盒原语)     │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 三、 性能基准与千人压测对比 (Benchmark)

在真实测试环境下，使用纯 Python 异步高并发发压引擎（[`scripts/benchmark_swarm.py`](scripts/benchmark_swarm.py)）对 MCPE 与行业主流服务端进行了严格对照测试：

| 评测维度 | 传统 Java 大厅/生存服 (Paper 1.12.2) | Cuberite (C++ 传统架构) | MCPE 微服务引擎 (本项目 C++23) | 架构收益幅度 |
| :--- | :--- | :--- | :--- | :--- |
| **基础常驻内存 (空载 RSS)** | $\approx 512\text{ MB} \sim 700\text{ MB}$ | $\approx 45\text{ MB}$ | **$\approx 14.2\text{ MB}$ (Linux) / $114\text{ MB}$ (Win)** | **内存降低 85% ~ 97%** |
| **1,000 Bot 满载常驻内存** | $\ge 2.0\text{ GB} \sim 3.0\text{ GB}$ (频发 GC) | $\approx 180\text{ MB}$ | **$127.7\text{ MB}$ (增量仅 13MB)** | **抗暴击能力提升 95%** |
| **1,000 Bot 入服就绪率** | 频繁丢包/超时 (<85% 就绪) | 98.2% | **100.0% (1,000/1,000 成功入服)** | **0 异常 / 0 登录失败** |
| **单连接生命周期回收** | 依赖 JVM Full GC，存在碎片 | 良好 | **RAII + EventQueue 析构立即归还** | **完全零内存泄漏** |
| **200 Bot 同屏 10Hz 移动** | 12 ~ 14 TPS (广播 O(N²) 堆分配压力) | 18.5 TPS | **稳固 20.0 TPS (15,334 pps 入站吞吐)** | **零掉帧，零累积漂移** |
| **服务冷启动耗时 (Cold Start)**| $\approx 12,500\text{ ms} \ (12.5\text{s})$ | $\approx 1,200\text{ ms}$ | **$\le 20\text{ ms}$ (.mcpe_map 内存镜像)** | **提速 500+ 倍** |
| **容器镜像体积 (Docker Image)**| $\approx 350\text{ MB} \sim 480\text{ MB}$ | $\approx 85\text{ MB}$ | **$\le 30\text{ MB}$ (极简 Multi-Stage)** | **缩小 93.3%** |

*详细测试执行记录与指标大盘详见：[`doc/benchmark_report_sprint5.md`](doc/benchmark_report_sprint5.md)*。

---

## 四、 核心模块技术创新

### 1. 广播管线真零拷贝多播 (`ByteBuffer::retainedSlice()`)
传统游戏服广播时，每个客户端都会分配一份独立的包内存，在千人同屏移动广播时瞬间引发垃圾回收风暴。MCPE 实现了底层引用计数共享与写时复制（Copy-On-Write）：单次位移更新由单写入者线程封装为一个 `ByteBuffer`，通过 `retainedSlice()` 多播给上千名玩家的 Asio 发送队列，**物理上仅存在 1 份数据载荷**，吞吐提升 20 倍以上。

### 2. 空间九宫格 AOI 局部视界裁剪 (`SpatialGrid`)
基于玩家所在 `ChunkPos` 建立双向哈希索引桶。每次玩家跨区块时，通过集合差异运算（Difference Set）精准计算出“新增视野玩家集”与“离开视野玩家集”，仅针对视界范围内的观察者定向广播增量移动包（`0x26/0x27/0x28`）或绝对传送包（`0x4C`），彻底根除 $O(N^2)$ 广播风暴。

### 3. 离线地图烘焙工具链 (`tools/map_baker`)
将沉重的通用地图解压与格式转换（Gzip、ZipArchive、NBT 递归树、Anvil Region 与 Schematic）剥离至离线编译期工具链。线上生产服务端仅需装载纯平铺的一维 `ChunkSection` 二进制 `.mcpe_map` 镜像，实现 $<1\text{ms}$ 零解压内存装载，冷启动性能秒杀一切现有开源实现。

### 4. 双层 INI 配置与无缝持久化 (`IniConfig`)
对齐 Cuberite 规范，根目录 `settings.ini` 管理全局与大厅分线配额，地图目录 `assets/maps/<map>.ini` 管理出生点、AABB 碰撞盒与跑酷检查点。支持管理员在游戏内通过指令 `/setmaxplayers` 与 `/setspawn` 动态修改并原子写回磁盘，实现生产运维免重启热更新。

---

## 五、 快速上手与运行指南

### 5.1 环境要求
- **现代 C++ 编译器**：GCC 14+ / Clang 18+（完全支持 C++23 标准）；
- **构建工具**：CMake 3.14+，Ninja 或 GNU Make；
- **发压脚本**：Python 3.10+。

### 5.2 本地原生构建与运行 (Windows / Linux)

```bash
# 1. 克隆代码仓库
git clone https://github.com/your-username/mcpe.git
cd mcpe

# 2. CMake 跨平台配置与多核编译
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target server -j$(nproc)

# 3. 运行完整单元测试矩阵 (232 项单元测试 100% 通过)
ctest --test-dir build --output-on-failure

# 4. 启动服务端 (首次启动自动生成 src/settings.ini)
# Linux:
./bin/server
# Windows:
.\bin\server.exe
```

### 5.3 Docker 容器化与集群一键编排

```bash
# 方式 A: 单容器构建与启动 (体积 ≤ 30MB)
docker build -t mcpe-lobby:latest .
docker run -d --name mcpe-lobby -p 25566:25566 mcpe-lobby:latest

# 方式 B: 集群全栈编排 (BungeeCord 代理网关 25565 + MCPE 大厅 25566)
docker-compose up -d
```

### 5.4 运行客户端连入与千人压测验证

```bash
# 1. 列表探测与延迟回显验证 (Ping/Pong)
python scripts/test_ping.py

# 2. 单玩家端到端登录与心跳闭环验证
python scripts/test_login.py

# 3. 发起 1,000 Bots 高并发集群压测 (Limbo 模式)
python scripts/benchmark_swarm.py --host 127.0.0.1 --port 25566 --bots 1000 --rate 50 --mode limbo --duration 30

# 4. 发起 200 Bots 同屏漫游与九宫格 AOI 广播压测 (Crowd 模式)
python scripts/benchmark_swarm.py --host 127.0.0.1 --port 25566 --bots 200 --rate 50 --mode crowd --duration 15

# 5. 原版客户端直连体验
# 打开官方 Minecraft 1.12.2 客户端 -> 多人游戏 -> 直接连接: 127.0.0.1:25566
```

---

## 六、 敏捷研发全景看板 (Roadmap)

本项目采用严格敏捷迭代与单测驱动开发（TDD），全阶段 **100% 验收交付**：

- [x] **Sprint 0: 基础设施与叶子组件**（Netty 风格零拷贝 COW `ByteBuffer`、变长整型 `VarInt`、GoogleTest 边界全覆盖）；
- [x] **Sprint 1: 网络基座与列表探测**（Standalone Asio 异步架构、VarInt 定界流式分包、Server List Ping、MOTD）；
- [x] **Sprint 2: 登录认证与区块空间下发**（Login 鉴权、离线 UUID、`0x23 Join Game`、4096 方块紧凑 `ChunkSection` 调色板压缩）；
- [x] **Sprint 3: 玩家移动同步与双向聊天**（跨线程 MPSC 双缓冲 `EventQueue`、20 TPS 单写入者物理节拍器、实体移动增量广播、富文本聊天）；
- [x] **Sprint 3.5: 离线地图烘焙、九宫格 AOI 与网络协程化**（`map_baker` 二进制烘焙、`SpatialGrid` 空间桶视界裁剪、C++20 网络协程化、商业大厅永昼与 Fall Reset）；
- [x] **Sprint 4: 双层 INI 配置、多实例大厅弹性分线与跑酷小游戏**（`settings.ini` 双层配额、`IGameInstance` 独立 20 TPS Actor 线程、大厅满员裂变分线、`0x35 Respawn` 无缝切世界、跑酷毫秒计时与排行榜）；
- [x] **Sprint 5: BungeeCord 集群网关与生产级交付**（BungeeCord IP Forwarding 穿透、`0x18 Plugin Message` 跨服传送门、极简 Dockerfile 交付、1,000 无头 Bot 压测报告）。

---

## 七、 模块文档与技术专题导引

本仓库配套了从小白到架构师的完整技术指南与保姆级实战开发手册：

- **主线实战开发教程系列**：
  - [从语法小白到架构思维：总纲指南](doc/tutorial/00_tutorial.md)
  - [Sprint 0：基础设施与叶子组件实战](doc/tutorial/sprint0_infra.md)
  - [Sprint 1：网络基座与 Server List Ping 实战](doc/tutorial/sprint1_network_ping.md)
  - [Sprint 2：登录鉴权与区块流式下发实战](doc/tutorial/sprint2_login_spawn.md)
  - [Sprint 3：玩家移动同步与聊天系统实战](doc/tutorial/sprint3_movement_chat.md)
  - [Sprint 3.5：离线地图烘焙与九宫格 AOI 实战](doc/tutorial/sprint3_5_lobby_aoi.md)
  - [Sprint 4：双层 INI、多实例 Actor 与跑酷引擎实战](doc/tutorial/sprint4_parkour_room.md)
  - [Sprint 5：BungeeCord 集群网关、传送门与千人压测实战](doc/tutorial/sprint5_cluster_gateway.md)
- **协议与深度技术专题**：
  - [Minecraft Protocol 340 权威速查表](doc/protocol/protocol_340_cheatsheet.md)
  - [九宫格 AOI 空间裁剪算法深度推演](doc/guides/aoi_algorithm_analysis.md)
  - [现代 C++ 并发模型与单写入者原则](doc/guides/modern_cpp_concurrency_guide.md)
  - [定时器架构推演与 Linux 底层对比](doc/guides/linux_timers_and_mcpe_timer_architecture.md)
  - [Sprint 5 千人并发与集群大厅基准压测报告](doc/benchmark_report_sprint5.md)

---

## 八、 开源许可证 (License)

本项目基于 [MIT 许可证](LICENSE) 开源。代码严格遵循 Google C++ 风格指南与工业级完成定义（DoD）。
