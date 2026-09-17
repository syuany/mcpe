# Protocol 340（Minecraft 1.12.2）协议速查表 · 全库唯一权威参考

> **文档定位**：本项目（MCPE）所有协议相关文档与代码的**唯一事实来源（Single Source of Truth）**。
> `doc/00_tutorial.md`、`doc/01_tasks_breakdown.md`、`doc/sprints/*.md` 中出现的一切包 ID、字段类型、字节序、区块格式，均以本文为准；若与本文冲突，以本文为准，并回头修正其他文档。
>
> **协议版本**：`340`（Minecraft Java Edition **1.12.2**）
> **📁 本地全站存档**：[`doc/reference/wiki.vg/`](reference/README.md) —— wiki.vg 原站**已于实测中确认彻底下线**（TCP 连接被拒），故整站快照已随仓库本地留档；查阅原文请优先打开本地副本。
> **权威来源（原始 URL，仅作溯源）**：
> * 协议正文：`https://c4k3.github.io/wiki.vg/Protocol.html` → 本地 [`reference/wiki.vg/Protocol.html`](reference/wiki.vg/Protocol.html) / [`Protocol.txt`](reference/wiki.vg/Protocol.txt)
> * 区块格式：`https://c4k3.github.io/wiki.vg/Chunk_Format.html`（`oldid=14135`）→ 本地 [`reference/wiki.vg/Chunk_Format.html`](reference/wiki.vg/Chunk_Format.html)
> * 数据类型：`https://c4k3.github.io/wiki.vg/Data_types.html` → 本地 [`reference/wiki.vg/Data_types.html`](reference/wiki.vg/Data_types.html)
> * 实体元数据：`https://c4k3.github.io/wiki.vg/Entities.html` → 本地 [`reference/wiki.vg/Entities.html`](reference/wiki.vg/Entities.html)
> * 机器可读交叉校验：本地 [`reference/minecraft-data/1.12.2/protocol.json`](reference/minecraft-data/1.12.2/protocol.json)、[`reference/minecraft-data/1.12/blocks.json`](reference/minecraft-data/1.12/blocks.json)
>
> **引用规范**：wiki.vg 内容可自由用于实现服务端/客户端/机器人，但转载其内容需注明来源 `#mcdevs`。

---

## 一、为什么本项目锁定 protocol 340 而不是 1.8.8（protocol 47）

| 维度 | 1.8.8（protocol 47） | **1.12.2（protocol 340）** |
| :--- | :--- | :--- |
| 协议文档可得性 | wiki.vg 现行页面已不保留该版本正文，仅有零散历史版本链接，字段细节缺失、无法自洽核对 | 官方镜像页正文完整（含每个包的字段表、区块格式专页、样例实现） |
| 社区实现参考 | 极少 | 大量开源实现（Java / Python / C / C++ 均有 1.12.2 参考代码） |
| 协议复杂度 | 较低（无 Teleport Confirm、区块为定长 2 字节/方块） | 较高，但**更能训练真实工程能力**（调色板位压缩、传送确认、压缩包格式） |
| 本项目取舍 | ❌ 放弃 | ✅ **文档可查、可实现、可验收** |

> **决策结论**：本项目目标客户端统一为 **Minecraft Java Edition 1.12.2**，握手协议号填写 `340`。
> 客户端不匹配时的表现：状态查询阶段会显示红色 ✗ 与 "Outdated client/server"；登录阶段会收到 `Disconnect (login)` 并提示版本不符。

---

## 二、全局替换映射（1.8.8 → 1.12.2）

在阅读/维护旧文档时，按此表做**机械替换**：

| 项目 | 1.8.8 / protocol 47 | **1.12.2 / protocol 340** |
| :--- | :--- | :--- |
| 协议号（Handshake / Status JSON） | `47` | **`340`** |
| 版本名（Status JSON `version.name`） | `"1.8.8"` | **`"1.12.2"`** |
| 默认端口 | `25565` | `25565`（未变） |
| 外层包格式 | `[Length:VarInt][ID:VarInt][Payload]` | **未变** |
| 整数/浮点多字节序 | 大端（Big-Endian） | **未变** |
| Keep Alive 载荷类型 | `VarInt` | **`Long`（i64）** |
| Join Game `Dimension` 类型 | `int8` | **`Int`（4 字节）** |
| 出生点包是否带确认 | 否 | **是（`Teleport ID` + 客户端回 `Teleport Confirm`）** |
| 区块方块数据 | 每方块 2 字节小端（`blockId<<4\|meta`） | **调色板 + 位压缩 Long 数组（格式完全重写）** |
| 区块光照顺序 | 方块数据 → 方块光 → 天空光 → 生物群系 | **每个 Section 内：方块数据 → 方块光 → 天空光；生物群系在整列末尾** |
| 相对位移量化 | `Byte`，$1/32$ 方块 | **`Short`，$1/4096$ 方块** |
| 相对位移上限 | 4 方块 | **8 方块** |

---

## 三、各状态包 ID 全表（protocol 340 实测抽取）

> 约定：**C→S** = Serverbound（客户端发给服务端）；**S→C** = Clientbound（服务端发给客户端）。
> 表中 **加粗** 行是本项目必须实现的包。

### 3.1 Handshaking（握手）

| 方向 | ID | 名称 | 说明 |
| :--- | :--- | :--- | :--- |
| C→S | **`0x00`** | **Handshake** | 唯一的握手包，携带协议号与下一状态意图 |
| C→S | `0xFE` | Legacy Server List Ping | 非标准格式（无长度前缀、ID 为 u8），旧版客户端探测用。**本项目可忽略**（直接断连即可） |

> Handshaking 状态没有 S→C 包：客户端发出第一个包后协议立即切换状态。

### 3.2 Status（服务器列表查询）

| 方向 | ID | 名称 | 关键载荷 |
| :--- | :--- | :--- | :--- |
| C→S | **`0x00`** | **Request** | 无字段 |
| S→C | **`0x00`** | **Response** | `JSON Response: String (32767)` |
| C→S | **`0x01`** | **Ping** | `Payload: Long` |
| S→C | **`0x01`** | **Pong** | `Payload: Long`（原样回送） |

### 3.3 Login（登录）

| 方向 | ID | 名称 | 关键载荷 |
| :--- | :--- | :--- | :--- |
| C→S | **`0x00`** | **Login Start** | `Name: String (16)` |
| C→S | `0x01` | Encryption Response | 在线模式才需要；本项目离线模式不使用 |
| S→C | `0x00` | Disconnect (login) | `Reason: Chat` |
| S→C | `0x01` | Encryption Request | 在线模式才需要 |
| S→C | **`0x02`** | **Login Success** | `UUID: String (36)` + `Username: String (16)`；**收到即切换到 PLAY 状态** |
| S→C | `0x03` | Set Compression | `Threshold: VarInt`（可选，见 §5.4） |

### 3.4 Play — Clientbound（服务端 → 客户端，0x00 ~ 0x4F）

| ID | 名称 | 本项目用途 |
| :--- | :--- | :--- |
| `0x00` | Spawn Object | — |
| `0x01` | Spawn Experience Orb | — |
| `0x02` | Spawn Global Entity | — |
| `0x03` | Spawn Mob | 后续扩展 |
| `0x04` | Spawn Painting | — |
| **`0x05`** | **Spawn Player** | Sprint 4 玩家进出视野 |
| `0x06` | Animation (clientbound) | — |
| `0x07` | Statistics | — |
| `0x08` | Block Break Animation | 挖掘进度动画（可选） |
| `0x09` | Update Block Entity | — |
| `0x0A` | Block Action | — |
| **`0x0B`** | **Block Change** | Sprint 4 方块变更广播 |
| `0x0C` | Boss Bar | — |
| `0x0D` | Server Difficulty | 可选 |
| `0x0E` | Tab-Complete (clientbound) | — |
| `0x0F` | Chat Message (clientbound) | 聊天/系统提示 |
| `0x10` | Multi Block Change | 批量方块变更（优化项） |
| `0x11` | Confirm Transaction (clientbound) | — |
| `0x12` | Close Window (clientbound) | — |
| `0x13` | Open Window | — |
| `0x14` | Window Items | — |
| `0x15` | Window Property | — |
| `0x16` | Set Slot | 创造模式发物品（见 §7.6） |
| `0x17` | Set Cooldown | — |
| `0x18` | Plugin Message (clientbound) | 品牌回包（可选） |
| `0x19` | Named Sound Effect | — |
| `0x1A` | Disconnect (play) | 踢人 |
| `0x1B` | Entity Status | — |
| `0x1C` | Explosion | — |
| **`0x1D`** | **Unload Chunk** | 重发区块前必须先卸载（见 §6.6） |
| `0x1E` | Change Game State | — |
| **`0x1F`** | **Keep Alive (clientbound)** | Sprint 2 心跳，`Long` |
| **`0x20`** | **Chunk Data** | Sprint 3 地形下发 |
| `0x21` | Effect | — |
| `0x22` | Particle | — |
| **`0x23`** | **Join Game** | Sprint 2 进服 |
| `0x24` | Map | — |
| `0x25` | Entity | — |
| **`0x26`** | **Entity Relative Move** | Sprint 4 相对位移广播 |
| **`0x27`** | **Entity Look And Relative Move** | Sprint 4 移动+转头 |
| `0x28` | Entity Look | Sprint 4 仅转头 |
| `0x29` | Vehicle Move (clientbound) | — |
| `0x2A` | Open Sign Editor | — |
| `0x2B` | Craft Recipe Response | — |
| `0x2C` | Player Abilities (clientbound) | 创造模式飞行开关（建议发） |
| `0x2D` | Combat Event | — |
| **`0x2E`** | **Player List Item** | Sprint 4，**必须先于 Spawn Player 发送** |
| **`0x2F`** | **Player Position And Look (clientbound)** | Sprint 2 出生点，带 `Teleport ID` |
| `0x30` | Use Bed | — |
| `0x31` | Unlock Recipes | 可选 |
| **`0x32`** | **Destroy Entities** | Sprint 4 离开视野 |
| `0x33` | Remove Entity Effect | — |
| `0x34` | Resource Pack Send | — |
| `0x35` | Respawn | 死亡重生 |
| `0x36` | Entity Head Look | — |
| `0x37` | Select Advancement Tab | — |
| `0x38` | World Border | — |
| `0x39` | Camera | — |
| `0x3A` | Held Item Change (clientbound) | — |
| `0x3B` | Display Scoreboard | — |
| `0x3C` | Entity Metadata | Spawn Player 载荷的一部分 |
| `0x3D` | Attach Entity | — |
| `0x3E` | Entity Velocity | 击退等（可选） |
| `0x3F` | Entity Equipment | 手持物品展示（建议发） |
| `0x40` | Set Experience | — |
| `0x41` | Update Health | 建议进服后下发（避免客户端血量显示异常） |
| `0x42` | Scoreboard Objective | — |
| `0x43` | Set Passengers | — |
| `0x44` | Teams | — |
| `0x45` | Update Score | — |
| `0x46` | Spawn Position | 指南针/重生点（建议发） |
| `0x47` | Time Update | 昼夜时间（建议发） |
| `0x48` | Title | — |
| `0x49` | Sound Effect | — |
| `0x4A` | Player List Header And Footer | 可选 |
| `0x4B` | Collect Item | — |
| **`0x4C`** | **Entity Teleport** | Sprint 4 大跨度位移降级包 |
| `0x4D` | Advancements | — |
| `0x4E` | Entity Properties | — |
| `0x4F` | Entity Effect | — |

### 3.5 Play — Serverbound（客户端 → 服务端，0x00 ~ 0x20）

| ID | 名称 | 本项目用途 |
| :--- | :--- | :--- |
| **`0x00`** | **Teleport Confirm** | 必须消费，否则位置同步会错乱 |
| `0x01` | Tab-Complete (serverbound) | 可选 |
| `0x02` | Chat Message (serverbound) | 聊天 |
| `0x03` | Client Status | 重生/切维度，需容忍 |
| **`0x04`** | **Client Settings** | 进服后客户端立即发送，**必须容忍** |
| `0x05` | Confirm Transaction (serverbound) | 需容忍 |
| `0x06` | Enchant Item | — |
| `0x07` | Click Window | — |
| `0x08` | Close Window (serverbound) | 需容忍 |
| **`0x09`** | **Plugin Message (serverbound)** | 客户端发 `MC\|Brand`，**必须容忍** |
| `0x0A` | Use Entity | 攻击/交互实体 |
| **`0x0B`** | **Keep Alive (serverbound)** | Sprint 2 心跳应答，`Long` |
| **`0x0C`** | **Player** | 仅 `On Ground: Boolean` |
| **`0x0D`** | **Player Position** | Sprint 4 移动，**静止时每 20 tick 也会发一次** |
| **`0x0E`** | **Player Position And Look** | Sprint 4 移动+转向 |
| **`0x0F`** | **Player Look** | Sprint 4 仅转向 |
| `0x10` | Vehicle Move (serverbound) | — |
| `0x11` | Steer Boat | — |
| `0x12` | Craft Recipe Request | — |
| `0x13` | Player Abilities (serverbound) | 需容忍 |
| **`0x14`** | **Player Digging** | Sprint 4 挖掘，`Status == 2` 为挖穿 |
| `0x15` | Entity Action | 需容忍 |
| `0x16` | Steer Vehicle | — |
| `0x17` | Crafting Book Data | — |
| `0x18` | Resource Pack Status | 需容忍 |
| `0x19` | Advancement Tab | — |
| `0x1A` | Held Item Change (serverbound) | 需容忍 |
| `0x1B` | Creative Inventory Action | 创造模式取物品（见 §7.6） |
| `0x1C` | Update Sign | — |
| `0x1D` | Animation (serverbound) | 挥手 |
| `0x1E` | Spectate | — |
| **`0x1F`** | **Player Block Placement** | Sprint 4 放置方块 |
| `0x20` | Use Item | 使用物品（吃/射） |

> ⚠️ **路由器铁律**：上表标注"需容忍"的包，在状态机里必须有**显式分支（哪怕是空实现 + debug 日志）**。若你的路由实现是"未注册的 ID 一律断连"，1.12.2 客户端进服后立刻会被自己踢掉——这是最常见的自伤式 bug。

---

## 四、本项目关键包字段逐字对照

> 所有字段按**写入顺序**排列；未标注的整数均为大端。

### 4.1 Handshake（C→S `0x00`）

| 字段 | 类型 | 取值/说明 |
| :--- | :--- | :--- |
| Protocol Version | `VarInt` | 客户端填 `340` |
| Server Address | `String (255)` | 客户端输入的地址，服务端可忽略 |
| Server Port | `Unsigned Short` | 大端 u16，默认 25565 |
| Next State | `VarInt Enum` | `1` = Status，`2` = Login |

### 4.2 Status Response（S→C `0x00`）

```json
{
  "version": { "name": "1.12.2", "protocol": 340 },
  "players": { "max": 100, "online": 0 },
  "description": { "text": "§aMy C++ Minecraft Server §7[Powered by Asio]" }
}
```

* `JSON Response` 为 `String (32767)`：**VarInt 长度前缀 + UTF-8 字节**。
* 版本号不匹配时客户端列表会显示红色不兼容标记，务必填 `340` / `"1.12.2"`。

### 4.3 Login Success（S→C `0x02`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| UUID | `String (36)` | **带连字符的 36 字符字符串**（离线模式同为 v3 UUID） |
| Username | `String (16)` | 玩家名 |

* 离线 UUID：`UUID v3 = MD5("OfflinePlayer:" + username)`，把 16 字节哈希按标准格式化为 36 字符串。
* 该包发出后连接状态立即变为 **PLAY**。

### 4.4 Join Game（S→C `0x23`）

| 字段 | 类型 | 本项目取值 |
| :--- | :--- | :--- |
| Entity ID | `Int` | 全局自增正整数，从 1 开始 |
| Gamemode | `Unsigned Byte` | `1` = 创造（bit3 `0x8` 为极限模式标志，别误置位） |
| Dimension | **`Int`** | `0` = 主世界（**注意：4 字节，不是 1.8 的 int8！**） |
| Difficulty | `Unsigned Byte` | `0`和平 / `1`简单 / `2`普通 / `3`困难 |
| Max Players | `Unsigned Byte` | `100`（1.12.2 客户端已忽略该字段的显示用途） |
| Level Type | `String Enum (16)` | `"flat"` / `"default"` / `"largeBiomes"` / `"amplified"` / `"default_1_1"` |
| Reduced Debug Info | `Boolean` | `false`（否则 F3 信息被裁剪） |

### 4.5 Player Position And Look（S→C `0x2F`）

| 字段 | 类型 | 本项目取值 |
| :--- | :--- | :--- |
| X / Y / Z | `Double` ×3 | `0.0 / 64.0 / 0.0` |
| Yaw / Pitch | `Float` ×2 | `0.0f / 0.0f` |
| Flags | `Byte` | **`0x00`**（全绝对坐标）；位域：`0x01`X、`0x02`Y、`0x04`Z、`0x08`Y_ROT、`0x10`X_ROT |
| **Teleport ID** | **`VarInt`** | 自增计数器；**客户端必须回 `Teleport Confirm (0x00)`** |

* 该包会关闭客户端的 "Downloading Terrain" 界面。
* 客户端与服务器记录的坐标差 > 100 米会被踢 "You moved too quickly :( (Hacking?)"；X/Z 定点绝对值 > 3.2e7 会被踢 "Illegal position"。
* **Teleport Confirm 处理策略**：维护 `pending_teleport_id_`，收到确认后清空；若下一次要发新的 PPAL 而旧 ID 未确认，可重发（不要无限发，否则客户端位置抖动）。

### 4.6 Teleport Confirm（C→S `0x00`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Teleport ID | `VarInt` | 与收到的 PPAL 中的 ID 相同 |

### 4.7 Keep Alive（S→C `0x1F` / C→S `0x0B`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Keep Alive ID | **`Long`** | 服务端通常用毫秒时间戳；客户端原样回送 |

* ⚠️ **类型陷阱**：1.8.8 是 `VarInt`，1.12.2 是 **`Long`（8 字节）**。写错类型会导致后续字节流整体错位。
* 超时规则：客户端 20 秒收不到任何 Keep Alive 会自己断开并抛 "Timed out"；服务端 30 秒收不到客户端回包可踢人。本项目建议 **每 10 秒发一次、20 秒未响应即断开**。

### 4.8 Chunk Data（S→C `0x20`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Chunk X | `Int` | 方块坐标 ÷ 16 向下取整 |
| Chunk Z | `Int` | 同上 |
| Ground-Up Continuous | `Boolean` | 完整区块填 `true` |
| Primary Bit Mask | **`VarInt`** | 第 n 位为 1 表示包含 y=16n..16n+15 的 Section（1.8 是 u16，这里是 VarInt） |
| Size | `VarInt` | 后续 `Data` 的字节数 |
| Data | `Byte Array` | 见 §6 |
| Number of block entities | `VarInt` | 不支持方块实体时写 `0` |
| Block entities | `Array of NBT` | 可留空 |

### 4.9 Unload Chunk（S→C `0x1D`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Chunk X | `Int` | |
| Chunk Z | `Int` | |

> 对未加载的区块发送也是合法的。

### 4.10 Block Change（S→C `0x0B`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Location | `Position` | 64 位位域打包坐标，见 §5.3 |
| Block ID | `VarInt` | **全局调色板 state id**：`(blockId << 4) \| meta` |

* 常用方块：空气 `0`、石头 `(1<<4)\|0 = 16`、草方块 `(2<<4)\|0 = 32`、泥土 `(3<<4)\|0 = 48`、基岩 `(7<<4)\|0 = 112`、平原草方块可用 32。

### 4.11 Spawn Player（S→C `0x05`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Entity ID | `VarInt` | |
| Player UUID | `UUID` | **16 字节原始 UUID**（不是字符串！） |
| X / Y / Z | `Double` ×3 | |
| Yaw / Pitch | `Angle` | 1 字节，度 × (256/360) |
| Metadata | `Entity Metadata` | 至少写终结符 `0xFF` |

> ⚠️ **前置依赖**：必须**先**为该玩家发送 `Player List Item (0x2E, Action=0, add player)`，否则 Notchian 客户端**不会生成该玩家实体**（皮肤/披风数据也在那个包里）。

### 4.12 Player List Item（S→C `0x2E`，Action = 0 添加玩家）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Action | `VarInt` | `0` 添加 / `1` 改模式 / `2` 改延迟 / `3` 改显示名 / `4` 移除 |
| Number Of Players | `VarInt` | 数组长度 |
| UUID | `UUID` | 16 字节 |
| Name | `String (16)` | Action=0 |
| Number Of Properties | `VarInt` | **填 `0` 合法**（客户端会用默认皮肤） |
| Gamemode | `VarInt` | Action=0 |
| Ping | `VarInt` | 毫秒；负值显示"无连接"图标；<150ms 满格 |
| Has Display Name | `Boolean` | Action=0 时填 `false` |

### 4.13 Destroy Entities（S→C `0x32`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Count | `VarInt` | 数组长度 |
| Entity IDs | `Array of VarInt` | 待销毁实体 |

### 4.14 实体位移三件套

**Entity Relative Move（S→C `0x26`）**

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Entity ID | `VarInt` | |
| Delta X / Y / Z | **`Short` ×3** | `delta = (int16)((current - previous) * 4096)`；单位 $1/4096$ 方块 |
| On Ground | `Boolean` | |

* 单轴最大跨度：`32768 / 4096 = 8` 方块。**超过 8 方块必须改用 `Entity Teleport (0x4C)`**。

**Entity Look And Relative Move（S→C `0x27`）**：同上 + `Yaw/Pitch: Angle`（**为新角度绝对值，不是增量**）+ `On Ground`。

**Entity Teleport（S→C `0x4C`）**：`Entity ID: VarInt` + `X/Y/Z: Double` + `Yaw/Pitch: Angle` + `On Ground: Boolean`。

### 4.15 客户端移动包

**Player Position（C→S `0x0D`）**

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| X | `Double` | 绝对坐标 |
| Feet Y | `Double` | 脚部高度，通常为头部 Y − 1.62 |
| Z | `Double` | 绝对坐标 |
| On Ground | `Boolean` | |

* 客户端**每 20 tick（静止时也）至少发一次**：可用来实现"久未收到移动包即认为掉线"的看门狗。
* 服务端反作弊参考：比较本 tick 与上 tick 位移平方差，超过 100（滑翔翼 300）判定"moving too fast"。

**Player Position And Look（C→S `0x0E`）**：`X/Y/Z: Double` + `Yaw/Pitch: Float` + `On Ground`。
**Player Look（C→S `0x0F`）**：`Yaw/Pitch: Float` + `On Ground`。
**Player（C→S `0x0C`）**：仅 `On Ground: Boolean`。

### 4.16 Player Digging（C→S `0x14`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Status | `VarInt Enum` | `0` 开始挖 / `1` 取消 / **`2` 挖穿** / `3` 丢弃整叠 / `4` 丢弃单个 / `5` 射箭-进食收尾 / `6` 交换副手物品 |
| Location | `Position` | 方块坐标 |
| Face | `Byte Enum` | `0`−Y 底 / `1`+Y 顶 / `2`−Z 北 / `3`+Z 南 / `4`−X 西 / `5`+X 东 |

* Status ∈ {3,4,5,6} 时 Location 恒为 0/0/0、Face 恒为 −Y，**不要拿它当方块坐标处理**。
* Notchian 服务端只接受"方块中心到玩家脚部 1.5 单位内、且水平距离 6 单位内"的挖掘。

### 4.17 Player Block Placement（C→S `0x1F`）

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Location | `Position` | 被点击的方块坐标 |
| Face | `VarInt Enum` | 语义同 Player Digging |
| Hand | `VarInt Enum` | `0` 主手 / `1` 副手 |
| Cursor Position X/Y/Z | `Float` ×3 | 面内光标的 0~1 归一化坐标（用于楼梯、台阶朝向） |

* 真正放置的方块坐标 = `Location + face 法向量偏移`（除非点击的是可替换方块如草）。

### 4.18 Entity Metadata 格式（用于 Spawn Player 等）

```
loop {
  Index: Unsigned Byte   // 0xFF 表示数组结束
  Type:  VarInt          // 仅当 Index != 0xFF 时存在
  Value: 按 Type 解析     // 仅当 Index != 0xFF 时存在
}
```

| Type 值 | 含义 | 备注 |
| :--- | :--- | :--- |
| `0` | Byte | |
| `1` | VarInt | |
| `2` | Float | |
| `3` | String | |
| `4` | Chat | |
| `5` | Slot | |
| `6` | Boolean | |
| `7` | Rotation | 3 个 float：x/y/z 旋转 |
| `8` | Position | 64 位位域打包坐标 |
| `9` | OptPosition | Boolean + 可选 Position |
| `10` | Direction | VarInt：0 下 / 1 上 / 2 北 / 3 南 / 4 西 / 5 东 |
| `11` | OptUUID | Boolean + 可选 UUID |
| `12` | OptBlockID | VarInt：0 表示无，否则 `id << 4 \| data` |
| `13` | NBT Tag | |

> 来源：wiki.vg **Entities** 页（<https://c4k3.github.io/wiki.vg/Entities.html>）的 "Entity Metadata Format" 小节。
> 注意：**Entity Metadata 与方块的 metadata 是完全不同的概念**。不要求发送任何元数据字段，只要终结项 `0xFF` 正确发送即可；挂机玩家/压测机器人可以直接只写一个 `0xFF`。

---

## 五、数据类型要点

### 5.1 VarInt / VarLong

* 每字节低 7 位为数据，最高位为"是否还有后续字节"。
* `VarInt` 最多 5 字节、`VarLong` 最多 10 字节；超出即判定协议错误并断连（防 DoS）。
* 负数（如 `-1`）编码为 5 字节 `FF FF FF FF 0F`。
* 解码移位务必用 **无符号类型**，避免算术右移污染高位。

### 5.2 String / Chat / Identifier

* `String`：`VarInt 长度（字节数）+ UTF-8 字节`。
* `Chat`：本身是 JSON 的 `String`。
* `Identifier`：`String`，形如 `minecraft:stone`。

### 5.3 Position（64 位位域打包）

```
encoded = ((x & 0x3FFFFFF) << 38) | ((y & 0xFFF) << 26) | (z & 0x3FFFFFF)

val = read_u64()
x = val >> 38
y = (val >> 26) & 0xFFF
z = val << 38 >> 38          // 注意：C++ 中需显式做符号扩展
if (x >= (1<<25)) x -= (1<<26);
if (y >= (1<<11)) y -= (1<<12);
if (z >= (1<<25)) z -= (1<<26);
```

> C++ 实现建议：读为 `uint64_t`，用 `static_cast<int64_t>(v << 38) >> 38` 之类方式显式做符号扩展，别依赖 Java 的移位语义照抄。

### 5.4 压缩包格式（可选优化）

启用 `Set Compression (0x03, Threshold: VarInt)` 之后，所有后续包变为：

```
[Packet Length: VarInt]              // 以下全部内容的总长度
  [Data Length: VarInt]              // 未压缩的 (Packet ID + Data) 长度；0 表示本包未压缩
  [zlib compressed (Packet ID + Data)]   或   [Packet ID + Data]
```

* 未压缩长度 **≥ Threshold** 才允许压缩；小于阈值却压缩了会被客户端断连。
* 想关闭压缩：发 `Set Compression` 且 `Threshold` 为负值，或干脆不发。
* **本项目排期**：Sprint 1~4 一律使用**不压缩**格式（不发 `Set Compression`），压缩作为 Sprint 5 的带宽优化任务。

### 5.5 其它约定

* `Angle`：1 字节，`(度 * 256 / 360)` 取整。
* `UUID`：16 字节原始值（`Login Success` 例外，为 36 字符字符串）。
* `Boolean`：1 字节 `0x00/0x01`。
* 所有多字节数值均**大端**；**唯一的小端场景在 1.8 的区块方块数据里，1.12.2 已不存在**（Long 数组同样是大端）。

---

## 六、区块格式（protocol 340 重写要点）

### 6.1 总览

```
Data = [ChunkSection] × (Primary Bit Mask 中 1 的个数，自底向上)
     + Biomes (256 字节，仅当 Ground-Up Continuous == true)
```

**单个 ChunkSection 结构**：

| 字段 | 类型 | 说明 |
| :--- | :--- | :--- |
| Bits Per Block | `Unsigned Byte` | 每方块占用位数 |
| Palette | 变长 | 见 §6.2 |
| Data Array Length | `VarInt` | 后续 long 的个数 |
| Data Array | `Long[]` | 4096 个方块状态的位压缩数组，**大端 long** |
| Block Light | `Byte[2048]` | 每方块半字节 |
| Sky Light | `Byte[2048]` | **仅主世界存在**；地狱/末地不发送 |

### 6.2 调色板（Palette）

| Bits Per Block | 形式 | 编码 |
| :--- | :--- | :--- |
| `0 ~ 4` | 间接（按 4 位处理） | `Palette Length: VarInt` + `Palette: VarInt[]`（元素为全局 state id） |
| `5 ~ 8` | 间接 | 同上，位宽用给定值 |
| `≥ 9` | **直接（全局调色板）** | `Dummy Palette Length: VarInt`（**恒为 0**） |

* 全局调色板 state id = `(blockId << 4) | meta`，当前占 **13 位**。
* **本项目首选**：`Bits Per Block = 13` + `Dummy Palette Length = 0`，数据直接写全局 state id。理由：实现简单、必定正确；调色板压缩作为 Sprint 5 的带宽优化项。

### 6.3 位压缩数据数组（关键算法）

* 13 位/方块、4096 方块 → `4096 × 13 / 64 = 832` 个 long，**恰好整除、无需填充**。
* 位序：**低位在前**（little-endian bit order within long）；一个条目**可以跨越两个 long**（1.16 才引入"不跨界 + 填充"规则，1.12.2 必须支持跨界）。

```cpp
constexpr int kBits = 13;
constexpr int kBlocks = 16 * 16 * 16;
constexpr int kLongs = kBlocks * kBits / 64;      // 832
uint64_t data[kLongs] = {0};

for (int y = 0; y < 16; ++y)
  for (int z = 0; z < 16; ++z)
    for (int x = 0; x < 16; ++x) {
      int idx = (y << 8) | (z << 4) | x;           // 与 1.8 相同的空间局部性公式
      uint64_t value = static_cast<uint64_t>(state_id) & ((1u << kBits) - 1);
      int startLong   = (idx * kBits) / 64;
      int startOffset = (idx * kBits) % 64;
      int endLong     = ((idx + 1) * kBits - 1) / 64;
      data[startLong] |= value << startOffset;
      if (startLong != endLong)
        data[endLong] |= value >> (64 - startOffset);
    }
```

* 每个 long 按**大端 8 字节**写入网络流。

### 6.4 光照数组

* 2048 字节/section，每方块半字节：**偶数 x → 低半字节，奇数 x → 高半字节**。
* 遍历顺序：`for y: for z: for x += 2`，写入 `light[x] | (light[x+1] << 4)`。
* 建议：Block Light 全 `0x00`；Sky Light 全 `0xFF`（顶层露天最亮）。
* ⚠️ 若你在 Sky Light 里写满 `0x0F` 之类的值而位宽/调色板算错，客户端会把光照错读成方块数据，表现为"满屏乱方块"或直接 `ArrayIndexOutOfBoundsException` 闪退。

### 6.5 生物群系（Biomes）

* 仅当 `Ground-Up Continuous == true` 时发送，**256 字节**，索引 `z * 16 | x`。
* 平原填 `1`；不支持群系时推荐填 `127`（Void）。
* 1.12.2 是 **1 字节/群系**（1.13+ 才改成 4 字节 int，别提前实现错）。

### 6.6 Ground-Up Continuous 的语义与陷阱

| 取值 | 含义 |
| :--- | :--- |
| `true` | 用该包**创建/完整重建**区块：包含群系数据与 Bit Mask 指定的全部 Section；未指定的 Section 视为空 |
| `false` | 该包退化为"巨型 Multi Block Change"：只替换 Bit Mask 指定的 Section，**不发送群系**，未被指定的 Section 保持原样 |

* ⚠️ **对已加载区块重复发送 `Ground-Up Continuous = true` 会造成客户端内存泄漏**（F3 的 MultiplayerChunkCache 两侧数值会不一致）。正确做法：**先发 `Unload Chunk (0x1D)` 再发新的 Chunk Data**。
* 空 Section 不必发送（客户端自动推断为空气/最亮天空光），这是最主要的带宽节省点。
* 客户端**不渲染没有邻居的区块**：只发单个孤立区块时会看不到，但方块交互仍然有效——所以出生点必须发送成片区块网格（本项目取半径 2，共 25 个）。

---

## 七、1.12.2 行为差异与高发坑位（相对 1.8.8）

1. **`Teleport Confirm` 必须处理**：1.9 起服务端下发的 `Player Position And Look` 带 `Teleport ID`，客户端会回 `Teleport Confirm (0x00)`。若不消费该包，且你的路由器对未知包断连，就会随机掉线。
2. **Keep Alive 是 `Long`**：写错成 VarInt 会让整个包解析错位，客户端抛出 `DecoderException`。
3. **Join Game 的 `Dimension` 是 4 字节 Int**：按 1.8 的 int8 写会导致后续所有字段错位（`Difficulty` 变成 0，`Level Type` 变成乱码），客户端报解码异常。
4. **区块数据是位压缩的**：不再有"每方块 2 字节小端"。`Size` 必须与 `Data` 长度严格一致，Bit Mask 中 1 的个数必须与 `Data` 里 Section 的个数一致。
5. **Sky Light 只在主世界发送**：切到地狱维度后仍发 2048 字节天空光会让客户端解析错位。
6. **相对位移是 `Short`，单位 $1/4096$，上限 8 格**：用 Byte（1.8 语义）会导致坐标跳动，超过 8 格必须降级 `Entity Teleport (0x4C)`。
7. **`Spawn Player` 依赖 `Player List Item`**：漏发则客户端不生成实体（不报错、只是"人不见了"，极难排查）。
8. **创造模式必须回物品**：创造模式下客户端发送 `Creative Inventory Action (0x1B)`，服务端需回 `Set Slot (0x16)` 把物品放进快捷栏，否则玩家手里没方块、无法放置方块（`Player Block Placement` 会因空手被忽略）。
9. **客户端会主动发 `Client Settings (0x04)` 与 `Plugin Message MC|Brand (0x09)`**：必须容忍。
10. **`Player Position` 静止时也每 20 tick 发一次**：可据此实现移动看门狗，不必额外发明心跳。
11. **`Unload Chunk` 在重发区块前必须发送**：否则客户端 chunk 缓存泄漏。
12. **空手/空区块陷阱**：客户端不渲染缺少邻居的区块——出生点必须成片下发。

---

## 八、Sprint 验收用"协议正确性"检查单

- [ ] Handshake 里读到的 `Protocol Version` 为 `340`；Status JSON 回填 `"1.12.2"` / `340`。
- [ ] 状态查询连接在 Pong 之后能正常关闭，服务端无 `DecoderException` 类错误日志。
- [ ] `Login Success` 的 UUID 为 36 字符带连字符字符串，且与 `md5("OfflinePlayer:"+name)` 结果一致。
- [ ] `Join Game` 的 `Dimension` 占 4 字节（用抓包/Wireshark 逐字节核对）。
- [ ] `Player Position And Look` 带自增 `Teleport ID`，并能在日志中看到客户端回传的 `Teleport Confirm`。
- [ ] Keep Alive 双向均为 8 字节 Long，静置 5 分钟不掉线。
- [ ] `Chunk Data` 的 `Size` == `Data` 实际字节数 == `(Section 数 × 单 section 字节数) + 256`。
- [ ] 单 section（13 位直接调色板）字节数 = `1 + 1 + 2 + 832×8 + 2048 + 2048 = 10756`。
- [ ] 双人联调：`Player List Item` 先于 `Spawn Player`；`Destroy Entities` 在离开视野时下发。
- [ ] 无任何未注册包导致的"自己把自己踢下线"。

---

## 九、参考链接（本地存档优先）

> ⚠️ **wiki.vg 原站已下线**（实测 `https://wiki.vg/` 与 `https://wiki.vg/Protocol` 均为 TCP 连接被拒），第三方静态镜像也可能随时消失。
> 因此下列页面的**完整副本已随仓库留档**在 [`doc/reference/wiki.vg/`](reference/README.md)；每个页面同时提供 `.html`（保真、可离线浏览）与 `.txt`（可 grep、可读）。
> **查资料请优先打开本地副本**，右侧在线 URL 仅用于溯源。

| 页面 | 本地副本 | 原始 URL（可能已失效） |
| :--- | :--- | :--- |
| 协议主页（protocol 340，全部包 ID 与字段） | [`reference/wiki.vg/Protocol.html`](reference/wiki.vg/Protocol.html) | <https://c4k3.github.io/wiki.vg/Protocol.html> |
| 区块格式专页（调色板 / 位压缩 / 光照 / 群系） | [`reference/wiki.vg/Chunk_Format.html`](reference/wiki.vg/Chunk_Format.html) | <https://c4k3.github.io/wiki.vg/Chunk_Format.html> |
| 数据类型（VarInt / Position 位域 / 定点数） | [`reference/wiki.vg/Data_types.html`](reference/wiki.vg/Data_types.html) | <https://c4k3.github.io/wiki.vg/Data_types.html> |
| 实体元数据格式 | [`reference/wiki.vg/Entities.html`](reference/wiki.vg/Entities.html) | <https://c4k3.github.io/wiki.vg/Entities.html> |
| 服务器列表 Ping 细节 | [`reference/wiki.vg/Server_List_Ping.html`](reference/wiki.vg/Server_List_Ping.html) | <https://c4k3.github.io/wiki.vg/Server_List_Ping.html> |
| 协议加密（本项目离线模式不用） | [`reference/wiki.vg/Protocol_Encryption.html`](reference/wiki.vg/Protocol_Encryption.html) | <https://c4k3.github.io/wiki.vg/Protocol_Encryption.html> |
| 历史版本差异（升级/降级必查） | [`reference/wiki.vg/Protocol_History.html`](reference/wiki.vg/Protocol_History.html) | <https://c4k3.github.io/wiki.vg/Protocol_History.html> |
| 版本号 ↔ 协议号对照表 | [`reference/wiki.vg/Protocol_version_numbers.html`](reference/wiki.vg/Protocol_version_numbers.html) | <https://c4k3.github.io/wiki.vg/Protocol_version_numbers.html> |
| NBT 格式（方块实体 / 物品） | [`reference/wiki.vg/NBT.html`](reference/wiki.vg/NBT.html) | <https://c4k3.github.io/wiki.vg/NBT.html> |
| 机器可读协议定义（1.12.2，JSON/YAML） | [`reference/minecraft-data/1.12.2/protocol.json`](reference/minecraft-data/1.12.2/protocol.json) | <https://github.com/PrismarineJS/minecraft-data> |
| 机器可读方块/物品/群系表（1.12.2 用 1.12 的数字 ID） | [`reference/minecraft-data/1.12/blocks.json`](reference/minecraft-data/1.12/blocks.json) | <https://github.com/PrismarineJS/minecraft-data> |

**命令行检索示例**：

```bash
grep -rn "Chunk Data" doc/reference/wiki.vg/*.txt          # 搜全部页面
grep -n -A25 "Join Game" doc/reference/wiki.vg/Protocol.txt # 看某包的字段表
```

