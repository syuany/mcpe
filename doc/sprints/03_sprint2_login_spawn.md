# Sprint 2: 会话生命周期与虚空进服（Login & Void Spawn）

> **迭代目标**：打通 `LOGIN` 到 `PLAY` 状态流转，实现离线 UUID 算法，依序下发进服三件套（Login Success、Join Game、Position），挂载异步 KeepAlive 保活定时器，让玩家实体成功生成在虚空世界中，不崩溃、不掉线。  
> **预估工期**：4 ~ 5 天  
> **总任务数**：7 个微任务
>
> **1.12.2 迁移增量**：本 Sprint 的包 ID 与字段全部对齐 **protocol 340**——`Join Game` 由 `0x01` → **`0x23`**（`Dimension` 由 1 字节变为 4 字节 `Int`）、出生点包 `Player Position And Look` 由 `0x08` → **`0x2F`** 并新增 `Teleport ID`（必须消费客户端回传的 `Teleport Confirm (0x00)` 形成闭环）、`Keep Alive` 载荷由 `VarInt` 变为 **`Long`（8 字节）**（服务端 `0x1F` / 客户端 `0x0B`）。协议细节一律以 [`doc/03_protocol_340_cheatsheet.md`](../03_protocol_340_cheatsheet.md) 为准。

---

## 任务执行看板

- [ ] [TASK-2.1: Login Start 解析与玩家名校验](#task-21-login-start-解析与玩家名校验)
- [ ] [TASK-2.2: 离线模式 UUID 生成算法实现](#task-22-离线模式-uuid-生成算法实现)
- [ ] [TASK-2.3: Login Success 响应与状态切入 PLAY](#task-23-login-success-响应与状态切入-play)
- [ ] [TASK-2.4: Join Game 世界参数包组装](#task-24-join-game-世界参数包组装)
- [ ] [TASK-2.5: Player Position And Look 出生点下发](#task-25-player-position-and-look-出生点下发)
- [ ] [TASK-2.6: Keep Alive 心跳保活定时器机制](#task-26-keep-alive-心跳保活定时器机制)
- [ ] [TASK-2.7: Sprint 2 集成验收与虚空漫步测试](#task-27-sprint-2-集成验收与虚空漫步测试)

---

### TASK-2.1: `Login Start (0x00)` 解析与玩家名校验
* **任务类型**：协议业务 (Business)
* **预估耗时**：1.5 小时
* **任务设计指南**：
  * 当 `Handshake` 中 `Next State == 2` 时，进入 `ConnectionState::LOGIN`。
  * 客户端在此状态下发送的第一个包必须是 `Login Start (0x00)`。
  * 提取字段：`Username`（String 类型，前缀为 VarInt 长度）。
* **DoD（完成定义）**：
  * 校验 Username 长度在 1 到 16 之间，且只包含合法字符（`a-z, A-Z, 0-9, _`）。若不合法，主动断开连接。
  * 控制台打印带有玩家名称的连接接入日志。
* **推荐提交**：`feat(login): parse Login Start packet and validate player name`

---

### TASK-2.2: 离线模式 UUID 生成算法实现
* **任务类型**：底层算法 (Unit)
* **预估耗时**：1.5 小时
* **任务设计指南**：
  * 原版离线模式 UUID 的计算标准：
    `UUID = MD5("OfflinePlayer:" + username)`
  * 将 16 字节哈希值格式化为 36 字符的标准 UUID 字符串：`8-4-4-4-12` 格式（如 `c0611e38-460d-3277-bc67-083f2e1329a2`）。
* **DoD（完成定义）**：
  * 编写单元测试验证：针对 `"Steve"`，计算出的 UUID 必须与官方原版离线 UUID 结果精确吻合。
* **推荐提交**：`feat(auth): implement standard offline UUID generation`

---

### TASK-2.3: `Login Success (0x02)` 响应与状态切入 `PLAY`
* **任务类型**：协议业务 (Business)
* **预估耗时**：1 小时
* **任务设计指南**：
  * 构造 `Login Success (0x02)` 数据包：
    * 写入 36 字符的 UUID 字符串。
    * 写入玩家 Username 字符串。
  * 发送给客户端，并将当前 Session 状态变更为 `ConnectionState::PLAY`。
* **DoD（完成定义）**：
  * 客户端界面由 "Logging in..." 切换为 "Loading world..."。
* **推荐提交**：`feat(login): send Login Success and transition state to PLAY`

---

### TASK-2.4: `Join Game (0x23)` 世界参数包组装
* **任务类型**：世界协议 (Business)
* **预估耗时**：2.5 小时
* **任务设计指南**：
  * 状态切为 `PLAY` 后立即下发。字段顺序与类型必须分毫不差：
    * `Entity ID`: `Int`（4 字节，自增全局正整数，如 1）
    * `Gamemode`: `Unsigned Byte`（设为 `1`，创造模式）
    * `Dimension`: `Int`（**4 字节大端**，设为 `0`，主世界）
    * `Difficulty`: `Unsigned Byte`（设为 `1`，简单）
    * `Max Players`: `Unsigned Byte`（如 100）
    * `Level Type`: `String`（`"default"` / `"flat"` / `"largeBiomes"` / `"amplified"` / `"default_1_1"`；本项目设为 `"flat"`）
    * `Reduced Debug Info`: `Boolean`（设为 `false`）
* **DoD（完成定义）**：
  * 客户端解析该包无异常，客户端控制台无 `DecoderException`。
  * 用 Wireshark 抓包逐字节核对 `Dimension` 占 4 字节。
* **避坑指南**：
  * ⚠️ **维度字段类型**：1.12.2 的 `Dimension` 是 **`Int`（4 字节大端）**（`-1` 为下界，`0` 为主世界，`1` 为末地），**不再是 1.8（protocol 47）的带符号单字节 `int8`**。写成 1 字节会导致后续所有字段错位（`Difficulty`、`Level Type` 全部读成乱码），客户端直接抛解码异常——这是 1.8 → 1.12 迁移中最常见的错误。
* **推荐提交**：`feat(play): construct and send Join Game packet`

---

### TASK-2.5: `Player Position And Look (0x2F)` 出生点下发
* **任务类型**：世界协议 (Business)
* **预估耗时**：2 小时
* **任务设计指南**：
  * 紧随 `Join Game` 之后下发玩家初始坐标：
    * $X$: `Double`（设为 0.0）
    * $Y$: `Double`（设为 64.0）
    * $Z$: `Double`（设为 0.0）
    * $Yaw$: `Float`（设为 0.0f）
    * $Pitch$: `Float`（设为 0.0f）
    * $Flags$: `Byte`（**必须填 `0x00`**，标记全部坐标为绝对值；位域：`0x01` X / `0x02` Y / `0x04` Z / `0x08` Y_ROT / `0x10` X_ROT）
    * `Teleport ID`: `VarInt`（**1.9+ 新增字段**；维护会话级自增计数器，每下发一次 PPAL 自增 1）
  * 在 PLAY 路由中消费客户端回传的 `Teleport Confirm (0x00)`（载荷为 `Teleport ID: VarInt`），比对后清空待确认状态并打印 debug 日志。
* **DoD（完成定义）**：
  * 客户端成功跳过 "Downloading Terrain" 进度界面，直接渲染出游戏画面（深蓝虚空与天空），人物在坐标 $(0, 64, 0)$ 生成。
  * 服务端日志中能看到客户端回传的 `Teleport ID` 与下发的 ID 一致（确认闭环成立）。
* **避坑指南**：
  * ⚠️ **浮点数类型错位**：坐标为 `Double`（8 字节），朝向为 `Float`（4 字节）。若写混，坐标解析偏移会导致客户端视距无限拉伸或直接报错卡死在加载界面。
  * ⚠️ **漏写 `Teleport ID`**：1.12.2 的 PPAL 尾部必须有自增的 `Teleport ID: VarInt`，漏写会让客户端解码越界并断连——这是 1.8 → 1.12 最容易漏掉的新增字段；同时必须消费客户端回传的 `Teleport Confirm (0x00)`，否则位置同步会错乱。
* **推荐提交**：`feat(play): send initial Player Position And Look packet`

---

### TASK-2.6: `Keep Alive` 心跳保活定时器机制
* **任务类型**：网络核心 (Core)
* **预估耗时**：2.5 小时
* **任务设计指南**：
  * 在 `TcpSession` 中挂载 `asio::steady_timer`，每 10 秒触发一次：
    * 向客户端下发 `Keep Alive (0x1F)`（载荷为 `Long`（8 字节）的毫秒时间戳或随机整型 ID）。
  * 监听客户端回发的 `Keep Alive (0x0B)`（PLAY 状态）：
    * 提取客户端回传的 ID，校验是否与发出的一致。
    * 更新最近心跳活跃时间戳。
  * 超时检测：若超过 20 秒未收到有效心跳回包，判定为网络假死，主动销毁连接。
* **DoD（完成定义）**：
  * 玩家进服后在虚空中挂机 2 分钟，连接保持稳定，不会因超时被强制踢出。
  * 抓包核对双向 Keep Alive 载荷均为 8 字节 `Long`。
* **避坑指南**：
  * ⚠️ **载荷类型写错会静默失败**：1.12.2 的 `Keep Alive` 载荷是 **`Long`（8 字节）**（1.8 的 protocol 47 中是 `VarInt`）。类型写错会导致流解析错位、客户端抛 `DecoderException` 并断连，**而服务端日志毫无异常**（看起来一切正常，玩家却掉线），极难排查。
* **推荐提交**：`feat(network): implement periodic KeepAlive timer and timeout detector`

---

### TASK-2.7: Sprint 2 集成验收与虚空漫步测试
* **任务类型**：集成验证 (Integration)
* **预估耗时**：1 小时
* **任务目标**：
  * 打开客户端点击加入服务器。
  * 连击两次空格开启创造模式飞行，在虚空中向不同方向飞行移动。
  * 按 F3 查看坐标变化与区块坐标。
* **实测联调实录（2026-10-05 01:04，玩家 `sunny` 接入）**：
  * **Server List Ping 探测**：成功解析包含 `\0FML\0` 的 Forge 握手，正常下发 JSON MOTD 与回显 Pong 时间戳；
  * **登录鉴权**：玩家 `sunny` 接入，基于 RFC-4122 v3 MD5 正确算出官方离线 UUID `a8fc6cfc-e3b6-31b2-9720-21288b4f7683`；
  * **进服三部曲**：成功依序下发 `Join Game (0x23)` 与 `Player Position And Look (0x2F)`（分配 `TeleportID: 1`）；
  * **传送闭环与高频移动包捕获**：客户端回发 `Teleport Confirm (0x00, ID: 1)` 完成闭环后，立即发送 `0x0E (Player Position And Look, 33 字节)`；
  * **修复闭环**：路由分发层已全面加白移动包族（`0x0C, 0x0D, 0x0E, 0x0F`），彻底消除误判断连，虚空漫步稳定无掉线。
* **DoD（完成定义）**：
  * 客户端游戏控制台无任何 Warning/Error，服务端连接状态正常维护，平稳运行。
* **推荐提交**：`chore: verify and pass Sprint 2 Void World milestone`

---

## Sprint 2 交付物验收检查单（Review Checklist）

- [ ] 离线模式 UUID 正确生成，与官方计算一致。
- [ ] 进服三部曲（Login Success -> Join Game -> Position）顺序无误。
- [ ] 解决了 "Downloading Terrain" 加载卡死问题。
- [ ] KeepAlive 心跳机制稳定运转，无超时断开。
- [ ] 准备进入 [Sprint 3: 区块模型与超平坦地形](file:///home/ubu/dev/my/mcpe/doc/sprints/04_sprint3_chunk_world.md)。
