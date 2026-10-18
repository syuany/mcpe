# Sprint 3.5 保姆级实战开发教程：离线地图烘焙、九宫格空间 AOI 裁剪、商业大厅机制与网络协程化 (Baked Map, Grid AOI, Commercial Lobby & C++20 Coroutines)

> **目标协议**：**Minecraft Java Edition 1.12.2（Protocol Version: 340）**  
> **前置里程碑**：Sprint 0（基础组件）、Sprint 1（网络基座与 Server List Ping）、Sprint 2（登录鉴权与区块空间渲染）、Sprint 3（20 TPS 物理主循环、MPSC 双缓冲队列、相对位移与聊天多播）已全部验收通过。  
> **本阶段核心目标**：作为承接 Sprint 3 基础联机与 Sprint 4 多实例小游戏引擎之间的关键架构升级里程碑（Sprint 3.5），完成四大核心基建：将沉重的 `.zip` / `.mca` / `.schematic` 地图解析剥离至 `tools/map_baker` 离线烘焙工具链，实现线上 `.mcpe_map` 二进制镜像 **$<1\text{ms}$ 零解压极速冷启动**；构建基于 `ChunkPos` 双向哈希桶与邻居差异集的 **`SpatialGrid` 九宫格 AOI 系统**，将全服 $O(N^2)$ 广播降维至 $O(k)$ 局部常数多播；闭环商业大厅 **虚空跌落平滑复位（Fall Reset）**、**永昼锁定 (`0x47`)** 与 **飞行防作弊 (`0x2C`/`0x13`)**；完成 **网络层 C++20 无栈协程化 + `retainedSlice()` 真零拷贝多播 + `PacketBuilder` 统一封包**。  
> **教学设计原则**：**透彻讲清“是什么、为什么、怎么做”**。严格对齐仓库真实源码与四层架构红线，零裸写魔数，零跨线程数据竞态。  
> **预估工期**：2 ~ 3 天 | **核心任务数**：4 个微任务（TASK-3.5.1 ~ TASK-3.5.4）

## 一、 心智模型跃迁与架构原理（是什么与为什么）

### 1.1 为什么严禁在线上服务端直接解压 `.zip` / `.mca` / `.schematic`？（离线烘焙 vs 运行时解析）

* **是什么**：
  Minecraft 常见的地图资产包含两类格式：
  1. **WorldEdit `.schematic` 建筑蓝图**：Gzip 压缩的 NBT（Named Binary Tag）复合标签树，内含 `Width`、`Height`、`Length` 以及三维连续字节数组 `Blocks`、`Data` 与可选扩展 `AddBlocks`；
  2. **原版 Anvil 世界存档（`.zip` 或 `region/r.x.z.mca` + `level.dat`）**：每个 `.mca` 区域文件以 4096 字节扇区为单位组织至多 $32 \times 32 = 1024$ 个 Chunk，每个 Chunk 载荷经 Zlib/DEFLATE 压缩后再封装多层嵌套 NBT 标签（`Level -> Sections -> Y / Blocks / Data / Add`）。
* **为什么不能把解析器塞进线上 `server`（痛点与危害）**：
  1. **拖慢冷启动与破坏内存红线**：线上大厅微服务的核心指标是 **常驻内存 $\le 30\text{MB}$、冷启动 $\le 50\text{ms}$**。若每次启动都在进程内解压多张地图的 Gzip/Zlib 流并动态构建树状 NBT 节点，不仅瞬间产生大量临时堆内存峰值与碎片，还会把冷启动时间拉长数十倍；
  2. **外部动态库耦合与攻击面扩大**：在线上二进制中链接 `zlib` 并暴露复杂的二进制流解析器，会增加容器镜像体积与潜在的畸形文件解析风险；
  3. **污染核心运行时代码**：将 1,200+ 行 PKZIP 归档读取、Zlib 流式解压、NBT 标签遍历与运行时的 20 TPS 物理引擎混在同一目标库中，严重违背**单一职责原则（SRP）**。
* **怎么做（工业级离线烘焙工具链 `tools/map_baker`）**：
  遵循现代游戏引擎的 **Asset Baking（资产离线烘焙）** 范式：
  - 将 `ZipArchive`、`NbtReader`、`AnvilLoader`、`SchematicLoader` 全部剥离至独立的命令行工具 `tools/map_baker`（编译产物 `./bin/map_baker`）；
  - 在离线阶段一次性完成解压、NBT 提取、方块全局状态 ID 合成（`block_id << 4 | meta`）以及出生点安全地面高度自适应修正（`World::adjustSpawnPointToSafeGround()`），导出为紧凑的自定义二进制快照 `assets/maps/lobby.mcpe_map`；
  - 线上 `mcpe_server` 启动时只需调用 `World::loadBakedMap()`，按固定二进制布局直接将连续的 `uint16_t blocks[4096]` 读入内存 `ChunkSection`，**零解压、零 NBT 遍历、零 Zlib 依赖，冷启动加载 54 个区块耗时 $<1\text{ms}$**！

### 1.2 为什么全服广播在百人同屏下会引发网络崩塌？（从 $O(N^2)$ 全量广播到九宫格 AOI 裁剪）

* **是什么**：
  **AOI（Area of Interest，兴趣范围/视野感知域）** 是多人在线游戏服务器的核心空间算法，用于决定“当玩家 A 发生状态变化时，应当把数据包发给哪些观察者（Observers）”，以及“当玩家 A 移动跨越区域时，哪些玩家新进入他的视野（Enter）、哪些玩家离开他的视野（Leave）”。
* **为什么 Sprint 3 的全房间广播不可持续**：
  在 Sprint 3 中，每当一名玩家移动，服务端会向房间内的**所有其他在线玩家**广播相对位移包：
  - 设在线人数为 $N$，每人每秒上传 20 次移动包；
  - 每秒钟全服下行发包总数为 $20 \times N \times (N - 1) = O(N^2)$；
  - 当 $N = 500$ 时，服务端每秒要投递 $20 \times 500 \times 499 \approx 5,000,000$ 个数据包！哪怕有零拷贝缓冲区，500 万次 `asio::post` 与内核写调用也会瞬间击穿网卡队列与 CPU。
* **为什么选择基于 `ChunkPos` 的九宫格网格法（Grid-based Chunk AOI）而非十字链表或四叉树**：
  1. **与 Minecraft $16 \times 16$ Chunk 体系 1:1 零成本对齐**：方块坐标转区块坐标仅需算术右移 `cx = x >> 4, cz = z >> 4`，无需维护平行的空间分割树；
  2. **免疫“中心广场人群扎堆”引发的洗牌风暴**：在大厅喷泉广场，往往有上百名玩家聚集在同一区块内挂机微动。若采用**十字链表法（Cross-List）**，玩家的微小坐标晃动会导致 X/Z 有序链表频繁发生节点逆序与冒泡交换，退化为 $O(N^2)$；而在**九宫格网格法**中，只要 `old_chunk == new_chunk`，跨区块判定在 `O(1)` 时间内直接短路跳过，开销为零；
  3. **极致 CPU Cache 亲和性**：`SpatialGrid` 底层使用连续内存的 `std::vector<uint64_t>` 存储会话 ID，避免链表/四叉树指针跳转导致的 L1/L2 Cache Miss。

### 1.3 为什么虚空跌落复位（Fall Reset）绝不能在网络线程发完 `0x2F` 就直接 `return`？

* **是什么**：
  浮空岛大厅四周均为无底虚空。当玩家失足跌落至警戒高度以下（行走模式 $Y < 20.0$、飞行模式 $Y < 0.0$）时，服务端不走原版扣血死亡流程，而是立即下发 `0x2F Player Position And Look` 将玩家平滑拉回出生点。
* **为什么网络线程直接 `return` 会造成严重的“跨线程空间分裂”Bug**：
  假设玩家从出生点区块 `(3, 3)` 一路跑到远端边缘区块 `(10, 10)` 并掉入虚空：
  - 若 `PlayHandler::handlePlayerMovement`（运行在 Asio 网络线程）检测到 $Y < 20.0$，仅向该客户端发送了拉回 `(3, 3)` 的 `0x2F` 数据包，然后直接 `return` 丢弃了本次移动事件；
  - 此时客户端画面确实回到了 `(3, 3)`，但运行在 20 TPS 物理主线程的 `EntityManager` 和 `SpatialGrid` 却毫不知情，依然认为该玩家停留在远端区块 `(10, 10)`！
  - **灾难后果**：该玩家回到出生点后，周围 `(3, 3)` 的玩家完全看不见他（未触发 `enter_chunks` 的 `0x05 SpawnPlayer`），而远在 `(10, 10)` 的玩家面前却立着他的幽灵模型！
* **怎么做（单写入者状态闭环铁律）**：
  当 `PlayHandler` 触发 Fall Reset 下发 `0x2F` 后，**绝不提前 `return`**，而是将 `move_event.position` 与 `move_event.rotation` 覆写为复位后的出生点坐标，并正常推入 `EventQueue`。这样在下一个 50ms 物理 Tick 中，`EntityManager::updatePlayerPosition` 会自然感知到玩家从 `(10, 10)` 瞬移回 `(3, 3)`，自动通过 `SpatialGrid::updatePlayerChunk` 触发旧视野销毁（`0x32`）、新视野生成（`0x2E` + `0x05`）与 `0x4C Entity Teleport` 同步！

### 1.4 为什么 `ByteBuffer` 必须区分 `clone()` 与 `retainedSlice()`？

* **是什么**：
  - `ByteBuffer::clone()`：分配全新的堆内存 `Storage`，并将可读字节 `memcpy` 过去（深拷贝）；
  - `ByteBuffer::retainedSlice()`：调用 `slice(readableBytes())`，构造一个新的轻量 `ByteBuffer` 视图对象，底层通过 `std::shared_ptr<Storage>` **共享同一块物理内存**（引用计数 `use_count()` 加 1，零堆分配、零 `memcpy`）。
* **为什么多播广播必须使用 `retainedSlice()`**：
  当 20 TPS 主线程构建好一个移动包或聊天包并向九宫格内的 $K$ 名观察者广播时，使用 `pkt.retainedSlice()` 可以让所有目标 `TcpSession` 的 `write_queue_` 共享同一份只读数据包内存；同时一旦某个缓冲视图发生写入扩容，`ByteBuffer::makeSpace()` 会检查 `storage_.use_count() > 1` 自动触发 **Copy-On-Write（写时复制）**，兼顾了极致零拷贝性能与多线程内存安全。

## 二、 空间数学、二进制文件格式与底层协议硬核推演

### 2.1 预烘焙地图镜像（`.mcpe_map`）二进制物理布局规范

为了让线上 `World::loadBakedMap` 实现零解析直读，`.mcpe_map` 采用定长文件头 + 紧凑 Section 连续数组的二进制布局：

| 阶段 | 字段名称 | 类型与字节大小 | 偏移量 / 大小 | 物理含义与校验契约 |
| :--- | :--- | :--- | :--- | :--- |
| **Header** | `Magic` | `char[4]` | `0x00` (4B) | 固定魔数 `'M', 'C', 'P', 'E'`，校验文件合法性 |
| **Header** | `Version` | `uint32_t` | `0x04` (4B) | 格式版本号，当前固定为 `1` |
| **Header** | `SpawnPoint` | `double` × 3 | `0x08` (24B) | 世界安全出生点坐标 $(X, Y, Z)$ |
| **Header** | `SpawnRotation` | `float` × 2 | `0x20` (8B) | 世界出生点初始视角 $(\text{Yaw}, \text{Pitch})$ |
| **Header** | `ChunkCount` | `uint32_t` | `0x28` (4B) | 镜像中包含的有效 Chunk 总数 $N$（文件头共 **44 字节**） |
| **Chunk[i]** | `ChunkX`, `ChunkZ` | `int32_t` × 2 | 8B | 当前区块的网格坐标 $(cx, cz)$ |
| **Chunk[i]** | `PrimaryBitMask` | `uint16_t` | 2B | 16 位掩码，第 $s$ 位为 `1` 表示第 $s$ 个 Section ($Y \in [16s, 16s+15]$) 非空 |
| **Chunk[i]** | `Biomes` | `uint8_t[256]` | 256B | $16 \times 16$ 生物群系数组 |
| **Chunk[i]** | `Sections` | `uint16_t[4096]` × $K$ | $8192\text{B} \times K$ | 仅连续写入 `PrimaryBitMask` 中置 1 的 $K$ 个非空 Section 的原始全局方块状态数组 |

### 2.2 Schematic 与 Anvil 方块全局状态 ID 合成公式与 AABB 求交粘贴

在 Minecraft 1.12.2 (Protocol 340) 中，网络传输与内存 `ChunkSection` 统一使用 **13 位全局方块状态 ID（Global Block State ID）**：
$$\text{StateID} = (\text{BlockID} \ll 4) \;\mid\; (\text{Metadata} \;\&\; 0\text{x0F})$$

#### 1. `.schematic` 三维数组一维线性寻址与高 4 位扩展合成
WorldEdit `.schematic` 中的方块按 **YZX 顺序（Y 最外层，Z 中间层，X 最内层）** 展平存储在长度为 $W \times H \times L$ 的字节数组中：
$$\text{index} = (y \times \text{Length} + z) \times \text{Width} + x$$
- 基础方块 ID（低 8 位）：$\text{base\_id} = \text{Blocks}[\text{index}]$；
- 扩展高位 ID（可选 `AddBlocks` 半字节数组，每字节存 2 个方块的高 4 位）：
  $$\text{high\_nibble} = \begin{cases} \text{AddBlocks}[\text{index} / 2] \;\&\; 0\text{x0F}, & \text{if } \text{index} \bmod 2 == 0 \\ (\text{AddBlocks}[\text{index} / 2] \gg 4) \;\&\; 0\text{x0F}, & \text{if } \text{index} \bmod 2 == 1 \end{cases}$$
  $$\text{block\_id} = \text{base\_id} \;\mid\; (\text{high\_nibble} \ll 8)$$
- 方块子数据（4 位）：$\text{meta} = \text{Data}[\text{index}] \;\&\; 0\text{x0F}$。

#### 2. 蓝图粘贴至区块的 AABB 包围盒求交算法 (`SchematicLoader::pasteToChunk`)
向单个目标 `Chunk(cx, cz)` 填充方块时，直接对 **Chunk 水平包围盒** $[16cx, 16cx+15] \times [16cz, 16cz+15]$ 与 **Schematic 世界包围盒** $[O_x, O_x+W-1] \times [O_z, O_z+L-1]$ 求交集：
$$\begin{aligned}
X_{\text{start}} &= \max(16cx, \; O_x), \quad & X_{\text{end}} &= \min(16cx + 15, \; O_x + W - 1) \\
Z_{\text{start}} &= \max(16cz, \; O_z), \quad & Z_{\text{end}} &= \min(16cz + 15, \; O_z + L - 1) \\
Y_{\text{start}} &= \text{clamp}(O_y, \; 0, \; 255), \quad & Y_{\text{end}} &= \text{clamp}(O_y + H - 1, \; 0, \; 255)
\end{aligned}$$
当 $X_{\text{start}} \le X_{\text{end}} \land Z_{\text{start}} \le Z_{\text{end}}$ 时，仅在交集立方体内循环写入非空气方块，达到**零浪费遍历**。

### 2.3 九宫格邻居差异集数学推导 (`SpatialGrid::computeTransition`)

设视野半径为 $R$（默认 $R = 1$，对应 $3 \times 3 = 9$ 个区块的九宫格）。定义中心区块 $\mathbf{C} = (C_x, C_z)$ 的切比雪夫（Chebyshev）邻域集合为：
$$\Omega(\mathbf{C}, R) = \left\{ (x, z) \in \mathbb{Z}^2 \;\middle|\; \max(|x - C_x|, \; |z - C_z|) \le R \right\}$$

当玩家从旧区块 $\mathbf{C}_{\text{old}} = (X_{\text{old}}, Z_{\text{old}})$ 跨越边界进入新区块 $\mathbf{C}_{\text{new}} = (X_{\text{new}}, Z_{\text{new}})$ 时：
1. **新进入视野区块集合（Enter Chunks）**：
   $$\text{enter\_chunks} = \Omega(\mathbf{C}_{\text{new}}, R) \setminus \Omega(\mathbf{C}_{\text{old}}, R) = \left\{ (x, z) \in \Omega(\mathbf{C}_{\text{new}}, R) \;\middle|\; |x - X_{\text{old}}| > R \;\lor\; |z - Z_{\text{old}}| > R \right\}$$
2. **移出视野区块集合（Leave Chunks）**：
   $$\text{leave\_chunks} = \Omega(\mathbf{C}_{\text{old}}, R) \setminus \Omega(\mathbf{C}_{\text{new}}, R) = \left\{ (x, z) \in \Omega(\mathbf{C}_{\text{old}}, R) \;\middle|\; |x - X_{\text{new}}| > R \;\lor\; |z - Z_{\text{new}}| > R \right\}$$

全过程仅需 $2 \times (2R+1)^2 = 18$ 次整数加减与绝对值比较，**零动态集合容器分配**。

### 2.4 Sprint 3.5 新增协议包物理结构速查（Protocol 340）

| 包 ID 与方向 | 协议名称 | 字段顺序与类型排布 | 核心用途与避坑要点 |
| :--- | :--- | :--- | :--- |
| **S→C `0x46`** | `Spawn Position` | `Location: Position (uint64_t)` | 设置世界指南针指向与默认出生点（64 位打包坐标：`((x & 0x3FFFFFF) << 38) \| ((y & 0xFFF) << 26) \| (z & 0x3FFFFFF)`） |
| **S→C `0x47`** | `Time Update` | `World Age: Long (int64_t)`<br>`Time of Day: Long (int64_t)` | 锁定大厅时间。**当 `Time of Day` 为负数（如 `-6000`）时，客户端停止太阳移动并固定在 `abs(-6000) = 6000`（正午）**！ |
| **S→C `0x2C`** | `Player Abilities` | `Flags: Byte (uint8_t)`<br>`Flying Speed: Float`<br>`Field of View Modifier: Float` | 控制无敌与飞行能力。`Flags` 位掩码：`0x01` 无敌、`0x02` 正在飞行、`0x04` 允许飞行、`0x08` 创造模式；默认 `Flying Speed = 0.05f`, `FOV = 0.1f` |
| **C→S `0x13`** | `Player Abilities` | `Flags: Byte (uint8_t)`<br>`Flying Speed: Float`<br>`Walking Speed: Float` | 客户端双击空格切换飞行时上行。若 `(Flags & 0x02) != 0` 但服务端 `session->canFly() == false`，必须立即回发 `0x2C` 强制拉回行走状态（防飞挂） |

## 三、 任务逐步实现指南（怎么做：接口契约与代码落地）

### TASK-3.5.1: 离线地图烘焙工具链 (`tools/map_baker`) 与 `.mcpe_map` 极速世界加载 (`World.h/.cc`)

将耗时且依赖 `zlib` 的 `.schematic` 与 `.mca`/`.zip` 解析逻辑封装在 `tools/map/` 下，统一复用 `common::NbtReader`；在 `mcpe::world::World` 中实现 `saveBakedMap()`、`loadBakedMap()` 与 `adjustSpawnPointToSafeGround()`，使线上 `mcpe_server` 启动时在 $<1\text{ms}$ 内直接将 `assets/maps/lobby.mcpe_map` 映射进内存。

### TASK-3.5.2: 基于 `ChunkPos` 的九宫格空间 AOI 裁剪与动态视界同步 (`SpatialGrid` + `EntityManager`)

在 `src/world/SpatialGrid.h/.cc` 中维护 `chunk_players_` 与 `player_chunks_` 双向哈希表。当 `EntityManager::updatePlayerPosition` 检测到 `old_chunk != new_chunk` 时调用 `grid_.updatePlayerChunk(session_id, new_chunk, 1)`：
- 对 `enter_chunks` 内的玩家双向互发 `0x2E (AddPlayer)` 与 `0x05 (SpawnPlayer)`；
- 对 `leave_chunks` 内的玩家双向互发 `0x32 (DestroyEntities)` 与 `0x2E (RemovePlayer)`；
- 在 `updateAndBroadcast()` 中仅查询 `grid_.getNearbyPlayers(cur_chunk, 1)`，通过 `pkt.retainedSlice()` 零拷贝投递位移包。

### TASK-3.5.3: 商业大厅环境初始化、虚空跌落保护 (Fall Reset) 与交互防作弊 (`PlayHandler` + `ChatHandler`)

1. **进服环境初始化 (`PlayHandler::onPlayerJoin`)**：下发 $11 \times 11$ 视距区块窗口、下发 `0x46 Spawn Position`、下发 `0x2C Player Abilities (Invulnerable)`、下发 `0x47 Time Update (6000, -6000)` 锁定正午永昼，以及 `0x2F Player Position And Look`；
2. **Fall Reset 状态闭环 (`PlayHandler::handlePlayerMovement`)**：当 $Y < 20.0$（飞行模式 $Y < 0.0$）时下发 `0x2F` 强制拉回出生点，并将 `move_event.position` 覆写为出生点坐标后继续推入 `EventQueue`，确保 `EntityManager` 与 `SpatialGrid` 状态一致；
3. **飞行切换与权威坐标读取 (`ChatHandler`)**：实现 `/fly [on|off]`、`/spawn` 与 `/setspawn`（从 20 TPS 主线程的 `EntityManager` 直接读取权威位姿）。

### TASK-3.5.4: 网络层 C++20 无栈协程化、真零拷贝切片 (`retainedSlice`) 与 `PacketBuilder` 统一封包

1. 将 `TcpServer::listenLoop`、`TcpSession::readerLoop` 与 `ClientSession::keepAliveLoop` 重构为 `asio::awaitable<void>` 协程，使用 `asio::as_tuple(asio::use_awaitable)` 消除异常栈展开开销；
2. 保留 `TcpSession::sendPacket` 的 `asio::post` + `write_queue_` 串行化发送队列，严格遵守 Asio 单 socket 禁止并发 `async_write` 的铁律；
3. 新增 `ByteBuffer::retainedSlice()` 与无会话绑定的 `PacketBuilder`，统一全库封包与零拷贝多播路径。

## 四、 验收门禁与端到端联调规范 (DoD)

1. **编译与离线烘焙**：`cmake --build build -j$(nproc)` 零告警通过，运行 `./bin/map_baker` 生成 `assets/maps/lobby.mcpe_map`；
2. **单元测试**：`ctest --test-dir build --output-on-failure` 全部通过（含 `test_world`、`test_spatial_grid`、`test_schematic`、`test_anvil_loader`）；
3. **端到端集成测试**：执行 `python3 scripts/test_lobby_integration.py` 验证 $11 \times 11$ 区块流、正午永昼、Fall Reset 拉回与九宫格 AOI 跨区块动态生成/销毁 100% 闭环。
