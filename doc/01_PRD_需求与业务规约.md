# 01_PRD_需求与业务规约 (Product Requirements Document & Business Rules)

> **文档标识**：`MCPE-DOC-PRD-01`  
> **文档标识**：`MCPE-DOC-PRD-01`  
> **版本**：v2.1.0 (轻量级大厅与集群网关微服务版)  
> **更新日期**：2026-10-18  
> **目标产品**：MCPE (High-Performance C++23 Minecraft Lightweight Lobby & Cluster Gateway Microservice Engine)  
> **对齐协议**：**Minecraft Java Edition 1.12.2（Protocol Version: 340）**

---

## 一、 产品概述与业务背景

### 1.1 背景与痛点
官方原版 Minecraft 服务端（Mojang Vanilla）及主流社区服务端（Spigot / Paper）均基于 Java 语言构建。在商业群组服（BungeeCord / Velocity 集群）及大厅导流中心等高密度并发场景下，存在以下技术与运维痛点：
1. **大厅实例内存开销巨大**：服主为了提供简单的导流大厅或挂机排队服，运行一个空白的 Java Paper 实例常驻内存动辄 **1GB ~ 2GB**。当集群拥有数十个大厅或导流子服时，空载内存浪费严重。
2. **实例冷启动极其缓慢**：Java 服务端插件加载与类加载通常需要 **15 ~ 30 秒**，无法实现动态按需秒级拉起与弹性扩缩容。
3. **原版生存机制过度沉重**：大厅与排队场景根本不需要红石计算、复杂的生物 AI 寻路、方块挖掘破坏及数百种物品合成配方，但 Java 服务端无法彻底剔除这些沉重包袱。

### 1.2 产品愿景与定位
MCPE 旨在利用 **现代 C++23 原生编译语言** 的零开销抽象与高性能网络能力，打造一款**专用于 Minecraft 群组服生态的高性能轻量大厅与跨服路由网关微服务引擎**：
* **极致轻量微服务**：无 JVM 开销，静态内存底噪 **$\le 30\text{MB}$**，冷启动时间 **$\le 50\text{ms}$**，相较传统 Java 大厅方案降低 95% 资源开销，极度利于高密度 Docker 容器化部署。
* **开箱即用的多实例大厅动态分线**：基于只读共享 `World` 内存架构与 Actor 线程模型，支持单进程弹性分线（`lobby-1`, `lobby-2`...），单分线增量内存 $<16\text{KB}$，支持全服总人数硬上限拦截与跨分线毫秒级平滑切换。
* **即插即用的商业集群兼容性**：原生支持 BungeeCord / Velocity IP Forwarding 穿透鉴权与 `0x18 Plugin Message (BungeeCord:Connect)` 跨服传送门路由，任何服主均可 5 分钟内无缝接入现有群组网络。
* **明确边界与非目标**：坚决不引入原版复杂红石电路、压力板传导、实体 AI 寻路与方块破坏物理掉落物，彻底收敛系统复杂度，本工程全量专注大厅与网关高并发服务。

---

## 二、 协议兼容性与准入规约

### 2.1 协议版本定义
* **目标客户端**：Minecraft Java Edition **1.12.2** 官方标准发行版（支持通过 ViaVersion 代理无缝接入高版本 1.16~1.21 客户端）。
* **协议版本号（Protocol Number）**：**`340`**。
* **传输层**：标准 TCP / IP 协议，默认监听端口 **`25566`**。
* **网关穿透协议**：兼容 BungeeCord IP Forwarding 扩展握手协议（携带真实客户端 IP、UUID 及皮肤 Properties）。

### 2.2 协议版本校验规约
1. 当客户端握手包（`Handshake 0x00`）携带的 `protocol_version != 340` 时：
   - 若状态转移为 `Status`（探测）：正常回传当前服务端的协议版本（340）及 MOTD，客户端列表正常显示延迟与说明。
   - 若状态转移为 `Login`（登录）：服务端下发 `Disconnect (Login) 0x00` 数据包，以格式化 JSON 提示版本不匹配，并安全断开连接。

---

## 三、 核心业务场景与功能用例规约 (Use Cases)

```
       [ 玩家客户端 ]                      [ MCPE 大厅微服务集群 ]                 [ 外部商业群组网络 ]
             │                                        │                                    │
    UC-01:   │ ── Server List Ping (0x00/0x01) ─────> │                                    │
    延迟探测  │ <─ MOTD JSON & Pong 回传 ───────────── │                                    │
             │                                        │                                    │
    UC-02:   │ ── Handshake (带 Bungee IP穿透) ─────> │                                    │
    登录认证  │ ── Login Start ──────────────────────> │                                    │
             │ <─ Login Success (0x02) ────────────── │                                    │
             │                                        │                                    │
    UC-03/04:│ <─ Join Game (0x23) ────────────────── │                                    │
    进服加载  │ <─ 0x20 Chunk Data (下发大厅地图) ──── │                                    │
             │ <─ Player Position And Look (0x2F) ─── │                                    │
             │                                        │                                    │
    UC-05/06:│ ── 玩家移动与聊天广播 ────────────────> │ ── (多播给同分线玩家) ───────────── │
    大厅社交  │                                        │                                    │
             │                                        │                                    │
    UC-07:   │ ── 输入 /lobby 2 或满员自动分流 ─────> │ [LobbyManager 跨分线迁移]           │
    大厅分线  │ <─ 0x35 Respawn (切换分线世界缓存) ─── │ [共享只读 World 挂载]               │
             │ <─ 0x20 Chunk Data (流式补齐区块) ──── │                                    │
             │ <─ 瞬移至新分线出生点 (0x2F) ───────── │                                    │
             │                                        │                                    │
    UC-08:   │ ── 踩中跨服传送门 (如：生存服) ──────> │                                    │
    集群切服  │ <─ 0x18 Plugin Message ("Connect") ─── │ ─────────────────────────────────> │
             │    (BungeeCord 拦截并自动转跳)          │        (玩家无缝转入 Java 生存服)   │
```

### 3.1 UC-01: 服务列表探测与 MOTD 呈现 (Server List Ping)
* **业务流程**：支持标准握手探测与 Ping/Pong 时间戳回传，客户端列表实时显示富文本 MOTD、当前在线人数与延迟信号。
* **完成定义 (DoD)**：真实 1.12.2 官方客户端能正常刷新延迟绿条，展示当前大厅集群中的在线玩家数量。

### 3.2 UC-02: 玩家准入与生命周期认证 (Login Lifecycle)
* **业务流程**：
  1. 握手阶段支持检测 BungeeCord 穿透标识（`host\0ip\0uuid`），若启用则提取玩家真实网络 IP 与正版 UUID；若未启用则根据用户名计算确定性离线 UUID。
  2. 校验用户名合法性（正则 `^[a-zA-Z0-9_]{1,16}$`）。
  3. 下发 `Login Success (0x02)`，会话无缝跃迁至 `Play` 状态。

### 3.3 UC-03: 玩家就位与出生点初始化 (Join Game & Spawn)
* **业务流程**：
  1. 服务端下发 `Join Game (0x23)`（维度 0，创造/冒险模式，默认世界类型 "flat"）。
  2. 下发玩家初始坐标 `Player Position And Look (0x2F)` 与服务端 Teleport ID。
  3. 客户端回传 `Teleport Confirm (0x00)` 闭环确认，激活玩家移动输入。

### 3.4 UC-04: 地图蓝图加载与区块流式下发 (Map Loading & Chunk Streaming)
* **业务流程**：
  1. 支持解析 WorldEdit 经典 `.schematic` 建筑蓝图文件（Gzip + NBT 三维方块数组）或预制区块，将其高效加载至内存 `Chunk` 结构中。
  2. 采用视距扩散算法向客户端流式下发 `Chunk Data (0x20)`。
  3. 大厅地图具备**全局只读保护**，客户端禁止破坏方块，杜绝地形腐化。

### 3.5 UC-05: 玩家移动同步与同屏实体可见性 (Movement & Entity Sync)
* **业务流程**：
  1. 服务端消费客户端上行的移动与朝向数据包（`0x0E`, `0x0F`, `0x10`）。
  2. 维护玩家在当前世界内的可见实体集合：当新玩家连入时，向同房间玩家下发 `Spawn Player (0x05)`；当玩家移动时广播相对移动包（`0x26`/`0x28`）；离开时下发 `Destroy Entities (0x32)`。

### 3.6 UC-06: 聊天广播与轻量指令系统 (Chat Multicast & Commands)
* **业务流程**：
  1. 玩家在客户端输入文本，发送 `Chat Message (0x02)`。
  2. 普通聊天：服务端格式化为标准 JSON 富文本，通过 `0x02 Chat (Clientbound)` 多播给同分线大厅内所有在线玩家。
  3. 指令交互：支持以 `/` 开头的快捷指令：
     - `/help`：查看所有可用指令帮助；
     - `/lobbies`：列出当前所有大厅分线及在线人数；
     - `/lobby [id]`：切换至指定大厅分线（默认切至 `lobby-1`）；
     - `/server <name>`：跨服传送至指定后端子服（如 `survival`）；
     - `/admin auth <secret>` / `/admin deauth` / `/admin dev [on|off]`：管理员鉴权与飞行调试模式；
     - `/setspawn` / `/setmaxplayers`：管理员热更新配置并实时持久化写回 INI。

### 3.7 UC-07: 多实例大厅分线生命周期与弹性扩缩容 (Lobby Sharding Lifecycle)
* **业务流程**：
  1. **弹性分线机制**：`LobbyManager` 统一管理 `lobby-1..N` 分线。当单大厅在线人数达到上限 `lobby_max_players` 时，自动裂变创建新大厅分线。
  2. **内存只读共享**：所有大厅分线共享同一份静态预烘焙地图 `World` 内存，单分线仅维护独立的 `EventQueue`、`EntityManager`、`SpatialGrid` 与 20 TPS 物理线程，增量内存开销 $<16\text{KB}$。
  3. **跨分线平滑切换**：玩家执行 `/lobby [id]` 时，服务端下发 `Respawn (0x35)` 重置客户端渲染缓存，并流式补发新分线区块与出生点传送包（0x2F），完成毫秒级无缝切线。
  4. **掉落安全熔断 (Fall Reset)**：服务端检测玩家高度；当 $Y < \text{fall\_reset\_y}$（如跌落虚空）时，自动将玩家重新瞬移回大厅出生点。

### 3.8 UC-08: 集群跨服传送门 (Cluster Gateway & Plugin Message Routing)
* **业务流程**：
  1. 大厅中支持配置空间传送门触发区（XYZ 坐标盒）或传送木牌。
  2. 当玩家步入传送门时，服务端向该客户端发送 `0x18 Plugin Message`：
     - 通道标识：`"BungeeCord"`
     - 数据内容：`writeString("Connect")` + `writeString(target_server_name)`。
  3. 外部 BungeeCord / Velocity 网关自动截获此包，将该玩家无缝平滑拉入指定的外部后端服务器（如 Java 生存服），服务端完成连接回收。

---

## 四、 业务规约与安全防挂边界 (Business & Security Rules)

| 规约编号 | 规则名称 | 详细判定与防御措施 | 违背处罚 |
| :---: | :--- | :--- | :--- |
| **BR-01** | **状态隔离与包合法性校验** | 严格限制每个阶段的合法 Packet ID，严禁跨阶段发包。 | 立即断开 TCP 连接 |
| **BR-02** | **大厅地形全局防破坏** | 玩家对大厅方块发起的任何 `Player Digging (0x14)` 或 `Block Placement` 均直接忽略/拒绝。 | 保持地图完整性，防恶意破坏 |
| **BR-03** | **Teleport ID 闭环锁定** | 下发位置瞬移包（0x2F）后锁定位置，直至收到 `Teleport Confirm`。 | 丢弃未确认期间的移动包 |
| **BR-04** | **移动速度防作弊（水平位移校验）** | 单 Tick 水平位移超过上限（如无药水疾跑速度 > 10m/s）判定为变速/飞行作弊。 | 触发强制拉回至上一合法坐标 |
| **BR-05** | **掉落安全熔断 (Fall Reset)** | 玩家高度跌出地图下界（如 $Y < \text{fall\_reset\_y}$）触发自动重置。 | 自动瞬移回大厅出生点 |
| **BR-06** | **心跳保活与幽灵会话回收** | 连续 30 秒未响应 `Keep Alive`。 | 主动释放会话与分线占用 |

---

## 五、 非功能性需求与性能指标 (NFR)

1. **常驻物理内存**：
   - 基础大厅启动常驻内存 RSS **$\le 30\text{MB}$**（相较 Java Paper 降低 95% 以上）。
2. **启动与切换耗时**：
   - 进程冷启动完成监听 **$\le 50\text{ms}$**；
   - 跨分线切换客户端黑屏等待时间 **$\le 300\text{ms}$**。
3. **并发与吞吐能力**：
   - 单节点在 4 核 8G 标准云主机上支撑 **1,000+ 玩家在线大厅闲逛与跨服导流**，Tick 耗时抖动 $\le 2\text{ms}$，维持恒定 20 TPS。
4. **交付与部署形态**：
   - 提供**单个纯静态无依赖二进制执行文件**；
   - 提供官方 `Dockerfile`，支持 `docker run -d -p 25566:25566 mcpe` 一键秒级启动。

