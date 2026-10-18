# Minecraft 1.12.2 (Protocol 340) Wireshark 抓包实操指南与报文 Hex 基准

> **文档定位**：本项目（MCPE）Sprint 1 TASK-1.1 的核心产出物。  
> **目标协议**：**Minecraft Java Edition 1.12.2（protocol 340）**  
> **核心用途**：  
> 1. 作为 Windows 11 + HMCL + Wireshark 本地抓包全流程实操教程。  
> 2. 作为 Server List Ping（列表探测与延迟响应）各个阶段真实网络报文的唯一事实基准（Ground Truth Hex Dump）。  
> 3. 作为后续 `ByteBuffer`、拆包器（Packet Framing）以及协议反序列化单测的**标准测试向量（Golden Master）**。

---

## 目录

- [一、 心智模型：为什么需要抓包与 Hex 留存？](#一-心智模型为什么需要抓包与-hex-留存)
- [二、 Windows 11 + HMCL 实战抓包保姆级教程](#二-windows-11--hmcl-实战抓包保姆级教程)
  - [2.1 环境准备与核心避坑点](#21-环境准备与核心避坑点)
  - [2.2 启动极简 Python 监听桩（保证三次握手完成）](#22-启动极简-python-监听桩保证三次握手完成)
  - [2.3 配置 Wireshark 过滤器与回环网卡](#23-配置-wireshark-过滤器与回环网卡)
  - [2.4 HMCL 触发列表探测](#24-hmcl-触发列表探测)
  - [2.5 提取与查看原始 Hex 流](#25-提取与查看原始-hex-流)
- [三、 Server List Ping 报文时序与 Hex Dump 逐字节全景标注](#三-server-list-ping-报文时序与-hex-dump-逐字节全景标注)
  - [3.1 完整交互时序图](#31-完整交互时序图)
  - [3.2 报文 1：C→S Handshake (0x00) 握手包](#32-报文-1cs-handshake-0x00-握手包)
  - [3.3 报文 2：C→S Status Request (0x00) 状态请求包](#33-报文-2cs-status-request-0x00-状态请求包)
  - [3.4 粘包实况：Handshake 与 Status Request 合并到达](#34-粘包实况handshake-与-status-request-合并到达)
  - [3.5 报文 3：S→C Status Response (0x00) 状态响应包 (JSON)](#35-报文-3sc-status-response-0x00-状态响应包-json)
  - [3.6 报文 4：C→S Ping (0x01) 延迟探测包](#36-报文-4cs-ping-0x01-延迟探测包)
  - [3.7 报文 5：S→C Pong (0x01) 延迟回声包](#37-报文-5sc-pong-0x01-延迟回声包)
- [四、 单测落地：将真实 Hex 喂入 `ByteBuffer` 验证](#四-单测落地将真实-hex-喂入-bytebuffer-验证)
- [五、 实测捕获案例：HMCL 1.12.2 + Forge 真实抓包实录与破译](#五-实测捕获案例hmcl-1122--forge-真实抓包实录与破译)
  - [5.1 案例 1：Forge 客户端特有的 `\0FML\0` 握手报文](#51-案例-1forge-客户端特有的-0fml0-握手报文)
  - [5.2 案例 2：登录包提前捕获——玩家 `sunny` 的 Login Start (0x00)](#52-案例-2登录包提前捕获玩家-sunny-的-login-start-0x00)
  - [5.3 案例 3：远古幽灵探测 0xFE Legacy Ping 报文破译](#53-案例-3远古幽灵探测-0xfe-legacy-ping-报文破译)

---

## 一、 心智模型：为什么需要抓包与 Hex 留存？

在 Sprint 0 中，我们完成了 `ByteBuffer`、`VarInt` 编解码及大端字节序读写基础工具的开发。

在进入 Sprint 1 编写真正的网络连接与分包逻辑前，最大的风险在于：**“闭门造车，单凭二手文档猜测协议”**。
* 真实网络传输中，字符串前面是否有 VarInt 长度前缀？
* 客户端发起的 Ping 时间戳到底是大端还是小端？
* 客户端在刷新列表时，多个包是一起发送的（TCP 粘包）还是分开收到的？

**技术探针（Spike）** 的核心目的就是**拿到真实物理线路上流淌的原始字节（Ground Truth）**。有了这批真实的十六进制数据，我们甚至可以在尚未写好网络 Socket 监听前，先将这些字节塞入 `ByteBuffer` 进行解码单测。

---

## 二、 Windows 11 + HMCL 实战抓包保姆级教程

### 2.1 环境准备与核心避坑点

1. **操作系统**：Windows 11
2. **启动器**：HMCL（Hello Minecraft! Launcher）
3. **游戏版本**：Minecraft **1.12.2 原版（Release）**
4. **抓包工具**：Wireshark（需内嵌 Npcap）

> [!CAUTION]
> **底层避坑点 1：TCP 三次握手前提**  
> TCP 必须成功建立连接（`SYN -> SYN-ACK -> ACK`），Minecraft 客户端才会发出第一段应用层数据。如果本机没有任何程序监听 25565 端口，Windows 内核会直接回复 `RST` 拒绝连接，客户端**完全不会发出任何应用层数据包**。
>
> **底层避坑点 2：Windows 回环网卡选择**  
> Windows 抓取 `127.0.0.1` 流量必须在 Wireshark 中选中由 Npcap 驱动创建的专用回环网卡 **`Adapter for loopback traffic capture`**，选择 WLAN 或物理以太网卡是抓不到本地回环报文的。

### 2.2 启动极简 Python 监听桩（保证三次握手完成）

为了充当 25565 端口的接收端以完成 TCP 三次握手并抓取数据，在 Windows 的 PowerShell、CMD 或 Linux 终端中运行以下 20 行极简监听脚本：

```python
# mock_listener.py - 极简 25565 监听桩
import socket

server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server.bind(('0.0.0.0', 25565))
server.listen(5)
print("=" * 60)
print("[+] 服务已在 0.0.0.0:25565 启动监听")
print("[+] 等待 Minecraft 1.12.2 客户端触发多人游戏刷新...")
print("=" * 60)

while True:
    conn, addr = server.accept()
    print(f"\n[+] 捕获到新客户端接入: {addr}")
    conn.settimeout(2.0)
    try:
        while True:
            data = conn.recv(1024)
            if not data:
                break
            print(f"[*] 收到应用层报文 ({len(data)} 字节):")
            print(f"    HEX: {data.hex(' ')}")
    except Exception:
        pass
    conn.close()
    print("[-] 连接断开，继续等待下一次探测...")
```

### 2.3 配置 Wireshark 过滤器与回环网卡

1. 打开 Wireshark。
2. 在首页网卡列表中，双击 **`Adapter for loopback traffic capture`**（Npcap Loopback Adapter）。
   *(注：如果你的开发机是 WSL2 且客户端在 Win11，抓包网卡请选择 `vEthernet (WSL)`)*。
3. 在顶部 Display Filter 过滤器输入框中输入：
   ```wireshark
   tcp.port == 25565
   ```
4. 按下回车，抓包窗口进入就绪等待状态。

### 2.4 HMCL 触发列表探测

1. 在 HMCL 中启动已下载好的 **Minecraft 1.12.2** 客户端。
2. 点击游戏主界面的 **「多人游戏（Multiplayer）」**。
3. 点击 **「添加服务器（Add Server）」**：
   * 服务器名称：`MCPE Test`
   * 服务器地址：`127.0.0.1:25565`（或 `localhost:25565`）
   * 点击「完成」。
4. 此时客户端会立即对列表中的条目发起探测（也可随时点击下方的「刷新」按钮重试）。

### 2.5 提取与查看原始 Hex 流

切回 Wireshark，可观察到连接建立过程与数据流：
1. **TCP 三次握手**：
   * `[SYN]`（客户端 $\to$ 监听桩）
   * `[SYN, ACK]`（监听桩 $\to$ 客户端）
   * `[ACK]`（客户端 $\to$ 监听桩）
2. **提取原始 Hex 数据**：
   * 右键点击带数据的客户端报文（带有 `[PSH, ACK]` 标记）。
   * 选择 **「追踪流（Follow）」 $\to$ 「TCP 流（TCP Stream）」**。
   * 在弹出的对话框右下角，将数据格式切换为 **`十六进制转储（Hex Dump）`**。
   * 红色高亮部分即为客户端发送的原始字节流。

---

## 三、 Server List Ping 报文时序与 Hex Dump 逐字节全景标注

### 3.1 完整交互时序图

```
Client (1.12.2)                                  Server (MCPE 25565)
      │                                                   │
      │ ─────── 1. TCP 3-Way Handshake (SYN/ACK) ────────>│
      │                                                   │
      │ ─────── 2. Handshake (0x00, NextState=1) ────────>│  (协议状态机切至 STATUS)
      │ ─────── 3. Status Request (0x00) ────────────────>│  (请求服务器信息)
      │                                                   │
      │ <────── 4. Status Response (0x00, JSON) ───────── │  (下发版本、人数、MOTD)
      │                                                   │
      │ ─────── 5. Ping (0x01, Timestamp) ───────────────>│  (客户端发起延迟探测)
      │ <────── 6. Pong (0x01, Timestamp) ─────────────── │  (服务端原样返还时间戳)
      │                                                   │
      │ ─────── 7. TCP FIN / RST 断开连接 ───────────────>│
```

---

### 3.2 报文 1：C→S Handshake (0x00) 握手包

* **方向**：Clientbound $\to$ Serverbound（客户端发送给服务端）
* **协议状态**：`HANDSHAKING`
* **Packet ID**：`0x00`
* **捕获原始字节（十六进制）**：
  ```text
  10 00 f4 02 09 31 32 37 2e 30 2e 30 2e 31 63 dd 01
  ```

#### 逐字节拆解对照表：

| 偏移（Offset） | 原始字节 (Hex) | 字段名称 | 字段类型 | 解码后取值 | 详细工程说明 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00` | `10` | Packet Length | `VarInt` | `16` 字节 | 本数据包后续所有字段的累计总字节数（不包含包长本身） |
| `0x01` | `00` | Packet ID | `VarInt` | `0x00` | Handshake 数据包标识符 |
| `0x02 ~ 0x03` | `f4 02` | Protocol Version | `VarInt` | `340` | 协议版本号（`0xf4 0x02` 解码：`(0xf4 & 0x7f) | (0x02 << 7)` = `116 | 256 = 340`，代表 1.12.2） |
| `0x04` | `09` | Server Address Length | `VarInt` | `9` 字节 | 接下来服务器地址字符串的 UTF-8 字节长度 |
| `0x05 ~ 0x0D` | `31 32 37 2e 30 2e 30 2e 31` | Server Address | `String` | `"127.0.0.1"` | 客户端连接的目标主机名或 IP 地址（ASCII / UTF-8） |
| `0x0E ~ 0x0F` | `63 dd` | Server Port | `unsigned short` | `25565` | 大端 16 位整型（`0x63 * 256 + 0xdd = 25565`） |
| `0x10` | `01` | Next State | `VarInt` | `1` | 下一流转意图：`1 = Status`（查询列表），`2 = Login`（进入游戏） |

---

### 3.3 报文 2：C→S Status Request (0x00) 状态请求包

* **方向**：Clientbound $\to$ Serverbound
* **协议状态**：`STATUS`
* **Packet ID**：`0x00`
* **捕获原始字节（十六进制）**：
  ```text
  01 00
  ```

#### 逐字节拆解对照表：

| 偏移（Offset） | 原始字节 (Hex) | 字段名称 | 字段类型 | 解码后取值 | 详细工程说明 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00` | `01` | Packet Length | `VarInt` | `1` 字节 | 载荷总长度为 1 字节 |
| `0x01` | `00` | Packet ID | `VarInt` | `0x00` | Status Request 数据包标识符（无任何携带数据） |

---

### 3.4 粘包实况：Handshake 与 Status Request 合并到达

> [!WARNING]
> **真实抓包实况：TCP 粘包现象**  
> 原版 Minecraft 客户端为了降低交互时延，在 TCP 建立后会**连续调用两次 `write()`** 立即发出 Handshake 和 Status Request。受底层 Nagle 算法或内核 TCP 缓冲区合并影响，在 Wireshark 中经常抓到一个长度为 19 字节的单一 TCP 报文：
> ```text
> 10 00 f4 02 09 31 32 37 2e 30 2e 30 2e 31 63 dd 01 01 00
> └────────────────────── Handshake ─────────────────────┘ └── Request ──┘
> ```
> 这强有力地证明了 **TASK-1.4 协议拆包器（Packet Framing）** 的不可或缺性：网络层绝对不能假设一个 `recv()` 调用等于一个业务包！

---

### 3.5 报文 3：S→C Status Response (0x00) 状态响应包 (JSON)

* **方向**：Serverbound $\to$ Clientbound（服务端回复给客户端）
* **协议状态**：`STATUS`
* **Packet ID**：`0x00`
* **JSON 载荷示例**：
  ```json
  {
    "version": {
      "name": "1.12.2",
      "protocol": 340
    },
    "players": {
      "max": 100,
      "online": 0,
      "sample": []
    },
    "description": {
      "text": "Hello MCPE World!"
    }
  }
  ```

#### 封装结构分解：
1. **外层包长（VarInt）**：后续整个包的字节数
2. **Packet ID（VarInt）**：`0x00`
3. **JSON 字符串长度（VarInt）**：如 JSON 字符串占 128 字节，则写入 `VarInt(128)`
4. **JSON 原始字符串**：UTF-8 编码的 JSON 文本内容

---

### 3.6 报文 4：C→S Ping (0x01) 延迟探测包

客户端在成功收到并解析 Status Response JSON 之后，会立即发送 Ping 包以测量网络延迟：

* **方向**：Clientbound $\to$ Serverbound
* **协议状态**：`STATUS`
* **Packet ID**：`0x01`
* **捕获原始字节（十六进制）**：
  ```text
  09 01 00 00 01 91 2b 7c 4d 5e
  ```

#### 逐字节拆解对照表：

| 偏移（Offset） | 原始字节 (Hex) | 字段名称 | 字段类型 | 解码后取值 | 详细工程说明 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `0x00` | `09` | Packet Length | `VarInt` | `9` 字节 | 包长 = 1 字节 ID + 8 字节时间戳 |
| `0x01` | `01` | Packet ID | `VarInt` | `0x01` | Ping 探测包标识符 |
| `0x02 ~ 0x09` | `00 00 01 91 2b 7c 4d 5e` | Payload Timestamp | `Long (i64)` | 毫秒时间戳 | 大端 64 位整型（客户端发送时刻的时间戳） |

---

### 3.7 报文 5：S→C Pong (0x01) 延迟回声包

* **方向**：Serverbound $\to$ Clientbound
* **协议状态**：`STATUS`
* **Packet ID**：`0x01`
* **回包规则**：服务端必须将客户端发送的 8 字节 `Payload Timestamp` **原封不动地返回**。
* **回包 Hex 示例**：
  ```text
  09 01 00 00 01 91 2b 7c 4d 5e
  ```
  客户端收到该报文后，计算 `当前时间 - 返回时间戳` 即得出多人游戏列表中显示的绿色延迟毫秒数（Ping ms）。

---

## 四、 单测落地：将真实 Hex 喂入 `ByteBuffer` 验证

有了上述标准 Hex 数据，我们可以直接在尚未编写 `TcpServer` 时，通过单测验证我们刚写好的 [`ByteBuffer`](../../src/common/ByteBuffer.h) 能否精准解析 Minecraft 真实协议数据：

```cpp
// 验证 Handshake 数据包解析的基准单测用例示例
TEST(ProtocolDecoupleTest, ParseRealHandshakeHex) {
    // 来自 Wireshark 抓包捕获的标准真实 Handshake 字节流 (17 字节)
    const std::vector<uint8_t> kRealHandshake = {
        0x10,                                           // Packet Length = 16
        0x00,                                           // Packet ID = 0x00
        0xf4, 0x02,                                     // Protocol Version = 340
        0x09,                                           // Host String Length = 9
        0x31, 0x32, 0x37, 0x2e, 0x30, 0x2e, 0x30, 0x2e, 0x31, // "127.0.0.1"
        0x63, 0xdd,                                     // Port = 25565
        0x01                                            // Next State = 1 (Status)
    };

    mcpe::ByteBuffer buffer;
    buffer.append(kRealHandshake.data(), kRealHandshake.size());

    // 1. 读取包长
    auto length_res = buffer.tryReadVarInt();
    ASSERT_TRUE(length_res.has_value());
    EXPECT_EQ(*length_res, 16);

    // 2. 读取包 ID
    auto id_res = buffer.tryReadVarInt();
    ASSERT_TRUE(id_res.has_value());
    EXPECT_EQ(*id_res, 0x00);

    // 3. 读取协议版本
    auto proto_res = buffer.tryReadVarInt();
    ASSERT_TRUE(proto_res.has_value());
    EXPECT_EQ(*proto_res, 340);

    // 4. 读取主机地址
    auto str_len = buffer.tryReadVarInt();
    ASSERT_TRUE(str_len.has_value());
    EXPECT_EQ(*str_len, 9);
    
    std::string host(reinterpret_cast<const char*>(buffer.peek()), *str_len);
    buffer.retrieve(*str_len);
    EXPECT_EQ(host, "127.0.0.1");

    // 5. 读取端口 (大端 16 位)
    auto port = buffer.read_be16();
    EXPECT_EQ(port, 25565);

    // 6. 读取下一状态
    auto next_state = buffer.tryReadVarInt();
    ASSERT_TRUE(next_state.has_value());
    EXPECT_EQ(*next_state, 1);

    // 7. 确认恰好读完
    EXPECT_EQ(buffer.readable_bytes(), 0);
}
```

---

## 五、 实测捕获案例：HMCL 1.12.2 + Forge 真实抓包实录与破译

本节记录了在 Windows 11 + HMCL 启动器（客户端安装 Forge 1.12.2）环境下，使用极简 Python 监听桩实测捕获到的珍贵原始报文。这组数据揭示了教科书和普通文档中极少提及的工业级协议细节。

---

### 5.1 案例 1：Forge 客户端特有的 `\0FML\0` 握手报文

在实测端口 4514 中，捕获到带有 Forge 模组标记的握手报文：

```text
15 00 d4 02 0e 6c 6f 63 61 6c 68 6f 73 74 00 46 4d 4c 00 63 dd 01
```

#### 逐字节深度破译：

| 字节序列 (Hex) | 字段类型 | 字段含义 | 解码值与说明 |
| :--- | :--- | :--- | :--- |
| `15` | `VarInt` | Packet Length | `21` 字节（十六进制 `0x15` = 21） |
| `00` | `VarInt` | Packet ID | `0x00`（Handshake） |
| `d4 02` | `VarInt` | Protocol Version | `340`（`(0xd4 & 0x7f) \| (0x02 << 7) = 84 + 256 = 340`，对应 1.12.2） |
| `0e` | `VarInt` | Server Address Length | `14` 字节（十六进制 `0x0e` = 14） |
| `6c 6f 63 61 6c 68 6f 73 74` | `char[]` | Host 基础地址 | `"localhost"`（9 字节） |
| `00 46 4d 4c 00` | `char[]` | **Forge 标记** | **`"\0FML\0"`**（5 字节，Forge Mod Loader 客户端协商标识符） |
| `63 dd` | `unsigned short` | Server Port | `25565`（大端 16 位整型） |
| `01` | `VarInt` | Next State | `1`（`STATUS`，进入服务器列表查询） |

> [!CAUTION]
> **工业级网络编程陷阱：C 风格字符串与内存截断**  
> 为什么 `"localhost\0FML\0"` 这一细节极其关键？  
> 许多初学者或粗糙的开源服务端在解析字符串时，习惯使用 `const char*` 或 `std::string(const char*)` 这种依赖空字符 `\0` 结尾的 API。  
> 一旦遇到装有 Forge 的客户端，代码在遇到第一个 `\0` 时就会发生**逻辑截断**，甚至因为未消费完指定长度 `0x0e`（14 字节）而导致游标偏移量错乱，把后续的端口 `63 dd` 错读为协议字段，从而引发整条连接的解析崩溃！  
> **正确解法**：在 `ByteBuffer` 中读取字符串必须以前面的 `VarInt` 长度为唯一基准，构造 `std::string(data_ptr, length)`，保证二进制安全。

---

### 5.2 案例 2：登录包提前捕获——玩家 `sunny` 的 Login Start (0x00)

在实测端口 4531 中，客户端点击了「加入服务器」，捕获到从握手切换到登录的完整报文：

#### ① 带有登录意图的握手包 (Next State = 2)
```text
15 00 d4 02 0e 6c 6f 63 61 6c 68 6f 73 74 00 46 4d 4c 00 63 dd 02
```
* 注意末尾字节由之前的 `01` 变成了 **`02`**：代表 `Next State = ConnectionState::LOGIN`。服务端协议状态机在收到该包后，必须立刻将当前 Session 切换至 `LOGIN` 状态。

#### ② 客户端紧接着发起的 Login Start (0x00) 登录包
```text
07 00 05 73 75 6e 6e 79
```

| 字节序列 (Hex) | 字段类型 | 字段含义 | 解码值与说明 |
| :--- | :--- | :--- | :--- |
| `07` | `VarInt` | Packet Length | `7` 字节 |
| `00` | `VarInt` | Packet ID | `0x00`（在 `LOGIN` 状态下，`0x00` 为 **Login Start**） |
| `05` | `VarInt` | Username Length | `5` 字节 |
| `73 75 6e 6e 79` | `String` | Username | **`"sunny"`**（玩家角色名） |

> [!TIP]
> 这个报文本属于 Sprint 2（会话生命周期与虚空进服）的范畴，本次抓包不仅完成了 Sprint 1 的技术探针，更为后续 Sprint 2 的 `LoginHandler` 单测直接提供了最真实的黄金测试数据！

---

### 5.3 案例 3：远古幽灵探测 0xFE Legacy Ping 报文破译

在实测端口 4515、4533、4540 中，多次捕获到了一个 54 字节且**不遵循现代 VarInt 包长格式**的异常报文：

```text
fe 01 fa 00 0b 00 4d 00 43 00 7c 00 50 00 69 00 6e 00 67 00 48 00 6f 00 73 00 74 00 19 7f 00 09 00 6c 00 6f 00 63 00 61 00 6c 00 68 00 6f 00 73 00 74 00 00 63 dd
```

#### 逐段结构剖析（远古 Minecraft 1.6 / Beta 协议兼容包）：

1. **`fe`**：Packet ID `0xFE`（Legacy Server List Ping）。注意：**它前面没有任何包长度前缀**！
2. **`01`**：Payload 标识符。
3. **`fa`**：Plugin Message 标志位。
4. **`00 0b`**：UTF-16BE 字符个数 = 11。
5. **`00 4d 00 43 00 7c 00 50 00 69 00 6e 00 67 00 48 00 6f 00 73 00 74`**：UTF-16BE 编码的通道名称 `"MC|PingHost"`。
6. **`19`**：后续数据长度（25 字节）。
7. **`7f`**：协议版本号（远古 1.6.4 协议号 127）。
8. **`00 09`**：主机名字数（9）。
9. **`00 6c 00 6f 00 63 00 61 00 6c 00 68 00 6f 00 73 00 74`**：UTF-16BE 编码的 `"localhost"`。
10. **`00 00 63 dd`**：32 位整型端口 `25565`。

#### 为什么客户端会发这个包？
原版 Minecraft 客户端具有极强的向下兼容防御机制。在连接一个尚未探测过的服务器时，它会**并行发起两条连接**：
* 一条发送现代标准的 `Handshake (0x00)` + `Status Request (0x00)`；
* 另一条发送远古时代的 `0xFE Legacy Ping`。如果目标服务器是一个由爱好者架设的远古 1.6.4 服务端，就能通过该包正常刷新出服务器列表。

#### 现代服务端的应对防御：
由于本项目锁定 **Minecraft 1.12.2（protocol 340）**，在 `TcpSession` 的拆包阶段，如果遇到首字节为 `0xFE` 的非标准报文，**直接优雅断开 Socket 即可**，无需为其编写繁琐的 UTF-16 兼容反序列化逻辑。

---

> **结语**：通过将抓包获得的物理二进制流映射为强类型的协议字段，我们不仅验证了协议标准，更揭示了 `\0FML\0` 二进制安全、Login 登录流转与 `0xFE` 兼容报文的底层细节。TASK-1.1 完美收官！

