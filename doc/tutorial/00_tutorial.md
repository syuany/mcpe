# 从语法小白到架构思维：Minecraft 协议网关与世界同步引擎实战指南

> **目标协议**：**Minecraft Java Edition 1.12.2（protocol 340）** —— 全项目统一版本；所有包 ID、字段类型与区块格式以 [`doc/03_protocol_340_cheatsheet.md`](03_protocol_340_cheatsheet.md) 为唯一权威参考。
> **面向读者**：刚学完 C++ 基本语法、读过或了解过一点 Muduo 网络库，渴望通过一个真实、高复杂度、具备硬核网络与并发特性的项目来提升大型项目架构能力的开发者。  
> **核心原则**：**授人以渔，不直接贴业务实现代码**。聚焦于架构推演、模块解耦、时序逻辑、接口设计契约、可验证的测试方法、底层避坑指南与权威资源。

---

## 目录
- [一、 心智转换：从“做习题”到“掌控大型工程”](#一-心智转换从做习题到掌控大型工程)
  - [1.1 为什么你学了语法和 Muduo 依然无从下手？](#11-为什么你学了语法和-muduo-依然无从下手)
  - [1.2 Muduo 与 Asio 的认知映射（Reactor 到 Proactor）](#12-muduo-与-asio-的认知映射reactor-到-proactor)
  - [1.3 核心工程方法论：自顶向下拆解，自底向上构建](#13-核心工程方法论自顶向下拆解自底向上构建)
- [二、 项目全局架构与模块解耦设计](#二-项目全局架构与模块解耦设计)
  - [2.1 现代 C++ 规范工程目录结构](#21-现代-c-规范工程目录结构)
  - [2.2 线程模型与数据流向总览图](#22-线程模型与数据流向总览图)
  - [2.3 调试开发工具箱准备](#23-调试开发工具箱准备)
- [三、 5 阶段保姆级研发路线与避坑指南](#三-5-阶段保姆级研发路线与避坑指南)
  - [阶段 0：基础组件与单测驱动（夯实地基）](#阶段-0基础组件与单测驱动夯实地基)
  - [阶段 1：网络基座、分包器与服务器列表展示（Server List Ping）](#阶段-1网络基座分包器与服务器列表展示server-list-ping)
  - [阶段 2：握手鉴权与虚空进服（Login & Void Spawn）](#阶段-2握手鉴权与虚空进服login--void-spawn)
  - [阶段 3：区块内存模型与超平坦地形下发（Chunk & World Sync）](#阶段-3区块内存模型与超平坦地形下发chunk--world-sync)
  - [阶段 4：并发解耦、20 TPS 节拍器与 AOI 视距广播](#阶段-4并发解耦20-tps-节拍器与-aoi-视距广播)
  - [阶段 5：压测机器人、性能 Profiling 与工业级调优](#阶段-5压测机器人性能-profiling-与工业级调优)
- [四、 权威学习资源与文档速查](#四-权威学习资源与文档速查)
- [五、 大型 C++ 工程能力自查清单（Checklist）](#五-大型-c-工程能力自查清单checklist)

---

## 一、 心智转换：从“做习题”到“掌控大型工程”

### 1.1 为什么你学了语法和 Muduo 依然无从下手？
1. **语法是单词，工程是文章**：学完语法，你懂 `class`、`unique_ptr`、`lambda`，但面对一个庞大系统时，你不知道谁该创建谁、谁持有谁、生命周期何时结束。
2. **Muduo 是现成的轮子，实际业务是复杂的装配厂**：学习 Muduo 时，教程通常把 `EventLoop`、`Channel`、`TcpServer` 都给你写好了，你只需要写个 `onMessage` 打印消息。但真实协议有粘包、半包、压缩、状态机、多线程数据投递、游戏世界状态同步，这之间的**胶水层与分层架构**才是工程的核心。
3. **陷入了“在 main.cc 顺序写代码”的线性思维**：新手容易想一口气从 `socket` 监听写到玩家走路。大型项目的正确打开方式是：**定义清晰的模块边界与接口契约，把不确定的大任务拆解成可独立单元测试的微任务**。

---

### 1.2 Muduo 与 Asio 的认知映射（Reactor 到 Proactor）

你学过一点 Muduo，这是你极大的优势！你已经具备了“非阻塞 I/O + I/O 多路复用 + 事件循环”的核心概念。但本项目采用 `Asio`，你需要建立心智映射：

| 概念维度 | Muduo（Reactor 模式） | Asio（Proactor 风格模拟） | 迁移理解与心智转变 |
| :--- | :--- | :--- | :--- |
| **事件循环** | `EventLoop::loop()` | `asio::io_context::run()` | 都是阻塞运行的事件驱动引擎，本质相同。 |
| **I/O 事件通知** | `epoll_wait` 返回后，告诉你 **“可读/可写了”**，你得自己调用 `read(fd, ...)` | 你发起 `async_read`，Asio 读完数据后才回调通知你 **“数据已读到缓冲区了”** | Muduo 是通知就绪；Asio 是通知完成。 |
| **连接抽象** | `TcpConnection` (继承自 `enable_shared_from_this`) | 自定义的 `TcpSession` (同样必须继承 `enable_shared_from_this`) | 生命周期管理哲学完全一致：连接的生命期由未完成的异步调用延续！ |
| **监听器** | `Acceptor` | `asio::ip::tcp::acceptor` | 均用于监听端口并生成新的连接套接字。 |
| **缓冲区** | `muduo::net::Buffer`（内部管理 readIndex / writeIndex） | `asio::streambuf` 或自定义 `ByteBuffer` | 游戏二进制协议强烈建议自己封装轻量 `ByteBuffer`，更可控。 |

---

### 1.3 核心工程方法论：自顶向下拆解，自底向上构建

* **自顶向下设计（Top-Down Design）**：
  在动手写代码前，先画出数据流：
  `网卡字节流 -> TcpSession 拆包 -> ProtocolPacket -> 投递给 World 逻辑主线程 -> 修改 World 状态 -> 生成广播包 -> 投递回网络线程发送`。
* **自底向上构建（Bottom-Up Implementation）**：
  写代码时，**绝对不要先碰网络**！
  1. 先写叶子模块：字节缓冲区 `ByteBuffer`。
  2. 再写算法模块：`VarInt` 编解码。
  3. 为它们编写简单的断言验证（单测），确认无 bug。
  4. 再搭网络外壳：接收网络包并用刚才写好的模块解码。
  *这样每一步都是踩在坚实的石阶上，而不是在一堆流沙上 debug。*

---

## 二、 项目全局架构与模块解耦设计

### 2.1 现代 C++ 规范工程目录结构

一个结构清晰的工业级 C++ 项目应该具备清晰的目录边界。推荐调整你的目录规划如下：

```
mcpe/
├── CMakeLists.txt              # 顶层 CMake 构建脚本
├── doc/                        # 技术文档与协议备忘
│   ├── 00_tutorial.md          # 本文：架构推演与阶段路线
│   ├── 01_tasks_breakdown.md   # 敏捷任务拆解看板（WBS）
│   ├── 03_protocol_340_cheatsheet.md  # ⭐ 协议 340（1.12.2）包 ID / 字段 / 区块格式唯一权威参考
│   └── reference/              # ⭐ 离线协议存档（wiki.vg 全站镜像 + 机器可读数据，详见其 README）
├── include/                    # 公共头文件（外部暴露或全局定义）
│   └── mcpe/
│       ├── common/             # 基础工具：ByteBuffer, VarInt, Endian
│       ├── network/            # 网络层：TcpServer, TcpSession, PacketFraming
│       ├── protocol/           # 协议层：Packet 定义、序列化、状态机定义
│       └── world/              # 业务世界层：World, Player, Chunk, ChunkSection
├── src/                        # 具体实现（内部逻辑）
│   ├── common/
│   ├── network/
│   ├── protocol/
│   ├── world/
│   └── main.cc                 # 启动入口：解析配置、启动线程、装配组件
└── tests/                      # 单元测试与压测工具
    ├── test_bytebuffer.cc      # 验证缓冲区与大端小端
    ├── test_varint.cc          # 验证 VarInt 边界与异常
    └── bot_runner.cc           # 阶段 5 的无头压测机器人
```

---

### 2.2 线程模型与数据流向总览图

在大型网络服务中，**网络 I/O 线程** 与 **业务计算线程** 必须解耦。绝对不能在网络回调中去遍历整个游戏世界，否则网络延迟会直接炸裂。

```
              ┌────────────────────────────────────────────────────────┐
              │                    I/O 线程池 (Asio)                    │
              │  ┌───────────────┐      ┌───────────────┐              │
客户端 TCP ──►│   │ TcpSession 1  │     │   TcpSession 2 │ ── (网络收发)│
              └──└──────┬────────┘──────└───────┬───────┘──────────────┘
                       │ 解包为 Packet         │ 解包为 Packet
                       ▼                       ▼
              ┌─────────────────────────────────────────────────────────┐
              │             跨线程任务队列（MPSC Queue / 管道）           │
              └────────────────────────┬────────────────────────────────┘
                                       │ 批量拉取事件 (Pop All)
                                       ▼
              ┌─────────────────────────────────────────────────────────┐
              │                   逻辑主线程 (Game World)                │
              │                                                         │
              │   Tick 定时器 (每 50ms 触发一次)                          │
              │   ├── 1. 处理所有玩家的客户端输入事件 (移动/挖掘/聊天)      │
              │   ├── 2. 更新游戏实体状态 (物理/AI/时间流逝)               │
              │   └── 3. AOI 视距计算，生成同步包广播                     │
              └────────────────────────┬────────────────────────────────┘
                                       │ 投递响应数据包
                                       ▼
                              返回各 Session 异步下发
```

---

### 2.3 调试开发工具箱准备

在动手前，工欲善其事，必先利其器：
1. **网络抓包分析器（Wireshark）**：
   * 监听本地 `Loopback: lo` 网卡。
   * 过滤器语法：`tcp.port == 25565`。
   * 在右键菜单中可直接以 Hex 查看原始 TCP 载荷，这是对齐协议的终极真理。
2. **Minecraft 客户端（1.12.2 / protocol 340）**：
   * 推荐使用 **HMCL (Hello Minecraft! Launcher)** 启动器，设置离线模式，下载 **1.12.2** 版本。
   * 📌 **版本一致性铁律**：本项目实现的是 **1.12.2（协议号 340）**。此处曾规划使用 1.8.8（协议号 47），但 1.8.8 的协议正文已从 wiki.vg 现行文档中下线，字段细节无法自洽核对，因此全项目统一升级到文档完整可得的最老版本：**1.12.2 / protocol 340**（<https://c4k3.github.io/wiki.vg/Protocol.html>）。用 1.8.8 客户端连本服务端会在登录阶段直接收到版本不符的 `Disconnect`，联调必须使用 1.12.2。
   * 本文所有包 ID、字段类型与区块格式，均以 [`doc/03_protocol_340_cheatsheet.md`](03_protocol_340_cheatsheet.md) 为唯一权威参考。
   * **至关重要的一步**：在启动器设置中开启 **“游戏启动时显示日志窗口（Game Log Console）”**。
   * 当你的协议字段写错时，Mojang 官方客户端会在控制台抛出极其明确的 Java 异常（如 `io.netty.handler.codec.DecoderException: VarInt too big` 或 `ArrayIndexOutOfBoundsException`），根据这个日志排查能省下 90% 的猜谜时间！
3. **编译器 Sanitizer（动态分析消毒器）**：
   * 在 `CMakeLists.txt` 中预留 Debug 构建选项：
     `-fsanitize=address,undefined -g`（排查越界与内存泄漏）。
     `-fsanitize=thread -g`（排查多线程竞态，注意 ASan 和 TSan 互斥，需分开编译运行）。

---

## 三、 5 阶段保姆级研发路线与避坑指南

---

### 阶段 0：基础组件与单测驱动（夯实地基）

#### 1. 目标定义（Definition of Done）
* 不碰网络代码，完全在独立源文件和单测中完成 `ByteBuffer` 和 `VarInt/VarLong` 工具的开发。
* 编写测试程序，验证任意数值经过 `编码 -> 解码` 后数据完全一致，能够正确抛出或处理非法数据。

#### 2. 核心心智模型与设计思路
* **为什么网络传输需要特定的 Buffer？**
  操作系统给你的网络数据是离散的原始字节流（`const char*` 或 `uint8_t*`）。你必须能够像“游标”一样顺序读写整数、浮点数、字符串。
* **接口设计参考抽象（伪接口规范）**：
  ```cpp
  class ByteBuffer {
  public:
      // 写入方法：自动处理主机字节序 -> 大端字节序（Big-Endian）
      void write_u8(uint8_t val);
      void write_be16(uint16_t val);
      void write_be32(uint32_t val);
      void write_be64(uint64_t val);
      void write_float(float val);
      void write_double(double val);
      void write_bytes(const uint8_t* data, size_t len);

      // 读取方法：带边界检查，大端字节序 -> 主机字节序
      uint8_t read_u8();
      uint16_t read_be16();
      uint32_t read_be32();
      uint64_t read_be64();
      float read_float();
      double read_double();

      // 指针与游标管理
      size_t readable_bytes() const;
      size_t read_index() const;
      void skip(size_t len);
  };
  ```
* **VarInt 编码原理**：
  * 每个字节只用低 7 位（bit 0~6）存储数据。
  * 最高位（bit 7，MSB）作为标志位：`1` 表示后续还有字节，`0` 表示这是最后一个字节。
  * 一个 32 位整型（`int32_t`）在 VarInt 编码下占用 1 到 5 个字节；64 位整型（`int64_t`）占用 1 到 10 个字节。

#### 3. 验证与测试方法
* 创建 `tests/test_varint.cc`，测试以下经典用例：
  * `0` -> 编码为单字节 `0x00`。
  * `1` -> 编码为 `0x01`。
  * `127` -> 编码为 `0x7F`。
  * `128` -> 编码为双字节 `0x80 0x01`。
  * `255` -> 编码为 `0xFF 0x01`。
  * `-1`（负数，特别关键！） -> 编码为 5 字节 `0xFF 0xFF 0xFF 0xFF 0x0F`。
* 编写测试断言：将上述数值编码入 Buffer，再从 Buffer 解码出来，断言（`assert`）前后值恒等。

#### 4. 易错点与避坑指南
* ⚠️ **VarInt 恶意包 DoS 漏洞（死循环或溢出）**：
  解码 VarInt 时，如果客户端持续发送最高位为 `1` 的字节，没有边界判断的代码会陷入死循环或导致左移溢出（Undefined Behavior）。**铁律**：`int32` 对应解码若超过 5 个字节、`int64` 超过 10 个字节，必须立即判定为协议错误并强行掐断连接！
* ⚠️ **浮点数的网络序传输**：
  很多初学者不知道怎么转 `float` 的字节序。标准做法是：使用 `std::memcpy` 将 `float` 拷贝到 `uint32_t`，使用 `htonl` 翻转字节序，写入 Buffer；读取时反向操作。**严禁使用 C 风格强制类型转换 `*(uint32_t*)&f`**，这会违反类型双发（Strict Aliasing Rule），在 `-O2` 优化下会产生灾难性代码。
* ⚠️ **符号位扩展与移位陷阱**：
  在处理 VarInt 移位时，操作变量务必使用**无符号类型**（`uint32_t` / `uint8_t`）。有符号数（`int32_t`）做右移操作在 C++ 中是算术右移（符号位补 1），会导致死循环或高位被污染。

#### 5. 错误契约：三种结果，三种处置（本阶段的真正产出）

解析函数的返回值设计，比算法本身更容易埋雷。**必须把下面三种情况区分开**，绝不能压成一个 `bool`：

| 结果 | 触发场景 | 发生频率 | 正确处置 |
| :--- | :--- | :--- | :--- |
| `kIncomplete` | TCP 半包：缓冲区里还凑不齐一个完整 VarInt | **极高，完全正常** | 保留读游标，等下一次 `read`；**不打 warn 日志、不断连、不抛异常** |
| `kMalformed` | 畸形/超上限：用满 5（或 10）字节仍带继续位 | 低，但**外部可控** | 记录并断开连接 |
| 程序 bug | 调用方没检查就硬读 | 应当为零 | 严格版读函数直接 fail-fast，别静默返回垃圾值 |

三条落地铁律：

1. **失败零副作用**：解析失败时既不消费任何字节，也不改写输出参数。这样"半包 → 原样返回 → 数据补齐后原地重试"永远安全——这正是拆包器（阶段 1）能成立的前提。建议用 RAII 读游标快照（`ByteBuffer::Mark`）把"多字段包体要么全成功、要么全回滚"变成默认行为。
2. **半包不是错误**：它不该产生异常、更不该产生日志。把它当异常处理，等于让所有网络抖动都走一遍抛/捕路径，既慢又误导代码结构。
3. **上限判断放在读第 6 / 第 11 个字节之前**，这样 `kMalformed` 和 `kIncomplete` 才是两个可判定的不同状态。

> **关于"不想用异常"**：表驱动异常（Linux/GCC、Clang）在**不抛**的路径上是零开销的，成本只在 `throw` 路径与二进制体积上。但这里用错误码不是性能原因，而是语义原因——**半包是最常见的正常情况**，不该用异常表达。异常留给"这条连接没救了"的终止性事件（协议版本不支持等），并在收包回调边界用**唯一一个** `catch` 统一踢人。参考实现见 `doc/reference/`（Cuberite 在其 `cProtocol_1_8_0::AddReceivedData` 里就是 `if (!ReadVarInt(len)) { ResetRead(); break; }`，失败即回滚、等下一包，全程零异常）。

#### 6. 分层落地：`bool` 只配待在谓词层

很多教程会说"底层返回 `bool` 表示成功失败"（Cuberite 的 `bool ReadVarInt32(UInt32&)` 就是这一派）。这个建议**对谓词成立，对解析结果不成立**：

| 层 | 签名 | 为什么是这个载体 |
| :--- | :--- | :--- |
| ① **谓词层** | `bool hasBytes(size_t n)` | 只回答"够不够"，**不存在错误分类**——`bool` 恰好够用，且最快、最直白。拆包器判断"整包到齐没有"就用它 |
| ② **解析层** | `std::expected<T, DecodeError> tryReadXxx() noexcept` | 有两种语义完全不同的失败，**一个 bit 装不下**。`Result<int32_t, uint8_t>` 只有 8 字节、走寄存器、`noexcept`，性能与 `bool + 出参` 无实测差别 |
| ③ **严格层** | `T readXxx()` | 前置条件已由调用方保证；违反即程序 bug，Debug 断言 / Release fail-fast |
| ④ **事务层** | `ByteBuffer::Mark` | 多字段包体"要么全成功、要么全回滚" |

> ⚠️ **只返回 `bool` 的代价（Cuberite 的实证）**：它的 `ReadVarInt32` 只有 `bool` 一条错误通道，于是**畸形包与半包无法区分**——超长输入最终只会让 `NEEDBYTES` 触底返回 `false`，被拆包层当成"数据还不够"而 `ResetRead()` 回滚，那段脏数据永远排不出去，连接既不报错也不断开，一直卡到缓冲区写满才被兜底踢掉。**这就是"用 bool 表达解析结果"的真实故障模式。**

> 🔧 **工具链前提：`std::expected` + clang 需要 clang ≥ 19（踩过一次，记下来）**
> libstdc++ 把它 gate 在 `bits/version.h` 的 `(__cplusplus >= 202100L) && (__cpp_concepts >= 202002L)`。
> clang **≤ 18** 只宣称 `__cpp_concepts = 201907L`，于是 libstdc++ 的 `<expected>` 整个不展开，
> clangd / clang-tidy / clang++ 统一报 `no template named 'expected' in namespace 'std'`
> （GCC 自己报 `202002L`，所以只有 clang 侧受影响）。
> LLVM 提交 [2875e24](https://lists.llvm.org/pipermail/cfe-commits/Week-of-Mon-20240408/565418.html) 明确写着
> "The forcing function is that libstdc++'s `<expected>` header is guarded by this macro"，
> 该改动**随 clang 19 发布**。
>
> 本项目的处置：保持用标准的 `std::expected`，把 clangd 升到 ≥ 19（Ubuntu 24.04 官方源已有 `clangd-19`）：
>
> ```bash
> sudo apt install -y clangd-19
> sudo ln -sf /usr/bin/clangd-19 /usr/local/bin/clangd   # /usr/local/bin 在 PATH 中先于 /usr/bin
> clangd --version                                       # 期望 19.x
> ```
>
> 让 `clangd` 这个名字指向 19 之后，`.vscode/settings.json` 里的 `"clangd.path": "clangd"` 无需改动。
> ⚠️ 换机器 / 上 CI 时若 clang ≤ 18，会直接编译失败（不只是编辑器报错）—— 要么装 clang ≥ 19，要么只用 GCC。

对照总表：

| 场景 | 载体 |
| :--- | :--- |
| "够不够"谓词（`hasBytes`） | `bool` |
| 网络输入解析（每包每字段） | `std::expected<T, DecodeError>`，`noexcept`；取值只用 `has_value()` / `operator*` / `value_or`，**不用 `.value()`**（错误时会抛） |
| 半包等待 | 返回值，零日志 |
| 畸形输入 | 返回值 → 调用方断连 |
| 自身不变量（不该发生） | `assert`（Debug）/ fail-fast（Release），**绝不用来校验不可信输入** |
| 终止性事件（版本不符、连接不可恢复） | 语义化命名的异常 + 边界处统一 `catch` |

---

### 阶段 1：网络基座、分包器与服务器列表展示（Server List Ping）

#### 1. 目标定义（Definition of Done）
* 启动服务监听 `25565` 端口。
* 启动 Minecraft 客户端，进入“多人游戏（Multiplayer）”，添加服务器 `127.0.0.1:25565`。
* 客户端列表中清晰展示：**绿色的延迟信号、当前在线人数/最大人数（如 0/100）、服务端的描述 MOTD（支持颜色字符）**。

#### 2. 协议时序图与状态机设计

```
客户端 (Client)                                   服务端 (Server)
      │                                                │
      │ ─── [TCP 三次握手建立连接] ───────────────────► │ (进入 HANDSHAKING 状态)
      │                                                │
      │ ─── Packet: Handshake (0x00) ────────────────► │ 解析: 协议号, 下一意图状态
      │     (NextState = 1 表示 STATUS)                 │ (切换状态机到 STATUS 状态)
      │                                                │
      │ ─── Packet: Status Request (0x00) ───────────► │
      │                                                │
      │ ◄── Packet: Status Response (0x00) ─────────── │ 返回 JSON 描述串 (MOTD, 人数)
      │                                                │
      │ ─── Packet: Ping (0x01) [时间戳: i64] ────────► │
      │                                                │
      │ ◄── Packet: Pong (0x01) [原样返回时间戳: i64] ── │ 客户端计算往返延时(RTT)，变绿灯
```

#### 3. 任务拆解与设计契约

1. **TcpSession 的生命周期管理**：
   * 必须继承 `std::enable_shared_from_this<TcpSession>`。
   * 内部持有一块接收缓冲区（用于处理 TCP 流式碎片）。
   * 必须通过 `shared_from_this()` 在每个 `async_read_some`、`async_write` 的 lambda 回调中持有自身的 `shared_ptr`，防止异步事件未完成时对象被析构。
2. **Minecraft 数据包拆包器（Packet Framing）**：
   * Minecraft 协议外层包裹格式：
     `[Packet Length: VarInt] + [Packet ID: VarInt] + [Data Payload: Bytes...]`
   * 注意：`Packet Length` 的长度值包含了 `Packet ID` 的长度 + `Data Payload` 的长度，但**不包含它自身所占用的字节数**！
   * 拆包算法逻辑伪代码思维：
     * 读取缓冲区。
     * 尝试解析一个 VarInt（`packet_len`）。如果缓冲区数据不够读取一个合法的 VarInt，等待下一次数据到来（半包）。
     * 检查当前缓冲区剩余字节数是否 $\ge$ `packet_len`。如果不满足，退回指针，等待后续数据（半包）。
     * 如果满足，切出一个完整的独立数据段进行协议处理；缓冲区游标前进，重复此过程（解决粘包）。
3. **组装 Status Response JSON 串**：
   * 返回的 JSON 格式规范（协议版本必须写 `340` / `"1.12.2"`，否则客户端列表显示红色不兼容标记）：
     ```json
     {
       "version": { "name": "1.12.2", "protocol": 340 },
       "players": { "max": 100, "online": 0 },
       "description": { "text": "§aMy C++ Minecraft Server §7[Powered by Asio]" }
     }
     ```
   * 整个响应包先将 JSON 写为字符串（Minecraft 协议字符串前缀为该字符串长度的 VarInt），打上 Packet ID `0x00`，再计算总长度加在最外层。
   * 1.12.2 的 `Ping (0x01)` / `Pong (0x01)` 载荷是 **`Long`（8 字节）**，不是 VarInt —— 这一点与 1.8 之后的多个版本一致，但常被写错。
   * **压缩包格式暂不启用**：本阶段不发送 `Set Compression (0x03)`，所有包一律走未压缩格式 `[Length:VarInt][ID:VarInt][Payload]`。压缩（`[Length][Data Length][zlib...]`）留到 Sprint 5 作为带宽优化项，避免在第 1 阶段就把拆包器复杂度翻倍。

#### 4. 易错点与避坑指南
* ⚠️ **Asio 的 `async_read` 与 `async_read_some` 混淆**：
  * `socket.async_read_some(buffer, ...)`：只要读到至少 1 个字节就会触发回调，返回实际读到的字节数。**必须自己维护累计缓冲区**！
  * 如果误认为它会等满缓冲区才返回，会导致解析 VarInt 时遇到非法指针越界。
* ⚠️ **“连续多次发包”导致的数据被覆盖**：
  * 初学者常常在 session 内部声明一个全局 `std::vector<uint8_t> send_buffer_`。
  * 发送第一段数据调用 `async_write`，在回调还未触发前，又调用 `async_write` 发送第二段数据，并将 `send_buffer_` 重新赋值或清空。这会导致前一次发送的数据被破坏，或者引发同时在同一个 socket 上发起两次未完成的写操作，直接触发 Asio 断言崩溃！
  * **解决方案**：引入**发送队列（Write Queue）**。每次发包推入队列，若当前没有写操作正在进行，则启动对队首的异步写入；待回调触发后，弹出队首，若队列非空继续写下一笔。

---

### 阶段 2：握手鉴权与虚空进服（Login & Void Spawn）

#### 1. 目标定义（Definition of Done）
* 客户端双击进入服务器，游戏界面走完“Connecting to the server...”和“Logging in...”。
* 玩家顺利进入游戏画面：屏幕中出现虚空背景，人物能够悬浮或向下坠落，**不闪退、不被踢出，客户端日志无抛出异常**。

#### 2. 核心状态转移与发包时序

```
客户端 (Client)                                   服务端 (Server)
      │ ─── Handshake (0x00) [NextState = 2] ────────► │ 状态切换至 LOGIN
      │ ─── Login Start (0x00) [Username: String] ────► │ 提取玩家用户名
      │                                                │ 生成离线 UUID
      │ ◄── Login Success (0x02) [UUID, Username] ──── │ 状态切换至 PLAY
      │                                                │
      │ ◄── Join Game (0x23) ───────────────────────── │ 分配 EntityID, 下发世界参数
      │ ◄── Player Position And Look (0x2F) ────────── │ 下发初始坐标 (X=0, Y=64, Z=0) + Teleport ID
      │ ─── Teleport Confirm (0x00) [Teleport ID] ────► │ 客户端确认传送（1.9+ 新增，必须消费）
      │                                                │
      │ [周期性定时器：每 10 秒]                          │
      │ ◄── Keep Alive (0x1F) [Long 随机 ID] ────────── │ 保活心跳下发
      │ ─── Keep Alive (0x0B) [Long 原样回传] ─────────► │ 心跳响应校验
```

#### 3. 关键字段与设计契约

1. **离线模式 UUID 的生成**：
   * 原版 Minecraft 在离线模式（Offline Mode）下，UUID 不是随机生成的，而是通过玩家名称的 MD5 哈希计算得出：
     `UUID = md5("OfflinePlayer:" + username)`。
   * 1.12.2 的 `Login Success` 包中，UUID 以 36 字符的标准字符串形式传输（带连字符，如 `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`）——**这是协议里的唯一例外**，其它所有包（如 `Spawn Player`、`Player List Item`）里的 UUID 都是 16 字节原始值，别再当字符串写。
2. **Join Game (0x23) 核心字段排布（严禁错乱）**：
   * `Entity ID`: `Int`（4 字节自增全局 ID，不可为 0，建议从 1 开始）。
   * `Gamemode`: `Unsigned Byte`（0: 生存, 1: 创造, 2: 冒险, 3: 旁观；bit3 `0x8` 是极限模式标志位）。推荐测试阶段填 `1`（创造模式，可以飞行防止直接掉虚空摔死）。
   * `Dimension`: **`Int`**（-1: 地狱, 0: 主世界, 1: 末地）。⚠️ 1.12.2 是 **4 字节整数**，不再是 1.8 的 `int8`，也不是 VarInt。
   * `Difficulty`: `Unsigned Byte`（0: 和平, 1: 简单, 2: 普通, 3: 困难）。
   * `Max Players`: `Unsigned Byte`（如 100；1.12.2 客户端已忽略它的显示用途）。
   * `Level Type`: `String`（填 `"flat"` 或 `"default"`；合法枚举：`default` / `flat` / `largeBiomes` / `amplified` / `default_1_1`）。
   * `Reduced Debug Info`: `bool`（填 `false`，方便在游戏里按 F3 看调试信息）。
3. **Player Position And Look (0x2F - 服务端下发)**：
   * 坐标：$X$ (`double`), $Y$ (`double`), $Z$ (`double`)。建议 $X=0.0, Y=64.0, Z=0.0$。
   * 视角：$Yaw$ (`float`), $Pitch$ (`float`)。
   * `Flags`: `uint8`。**必须填 `0x00`**（位域含义：`0x01`X 相对, `0x02`Y 相对, `0x04`Z 相对, `0x08`Yaw 相对, `0x10`Pitch 相对；填非零会被当作相对偏移，导致玩家坐标乱飘）。
   * **`Teleport ID`: `VarInt`（1.9+ 新增，1.8 没有）**：维护一个自增计数器，每发一次 PPAL 就 `++`；客户端会回一个 `Teleport Confirm (0x00)` 携带同一个 ID。**服务端必须消费这个包并记录确认状态**。
4. **PLAY 状态的"必答包"清单（1.12.2 特有）**：
   * 进服后客户端会立刻主动发来 `Client Settings (0x04)` 与 `Plugin Message MC|Brand (0x09)`，之后偶尔发 `Player Abilities (0x13)`、`Held Item Change (0x1A)`、`Client Status (0x03)`。
   * **路由器必须为这些 ID 准备显式分支（哪怕只打一行 debug 日志）**；如果你的实现是"未注册 ID 一律断连"，1.12.2 客户端会在进服后一秒内被你自己踢掉，而且日志上看像是客户端在乱发包。

#### 4. 易错点与避坑指南
* ⚠️ **客户端死锁卡在 "Downloading Terrain" 界面**：
  * 这是 99% 初学者会遇到的头号大坑！
  * **产生原因**：`Join Game` 之后，没有立刻下发 `Player Position And Look`，或者下发的包字段顺序、数据类型有偏差（例如把 `double` 误写为 `float`，或漏掉了 1.12.2 新增的 `Teleport ID`）。客户端一直在等待坐标确认，导致加载进度条永远卡死。
  * **排查手段**：用 Wireshark 抓 `lo` 网卡，逐字节核对 `Join Game` 后紧跟的那个包；`Dimension` 写成 1 字节是引发"后续全错位"的最常见元凶。
* ⚠️ **20 秒内必踢出（Timed Out）**：
  * Minecraft 客户端内嵌了严格的超时看门狗。进服后，服务端如果不发送 `Keep Alive`，或者服务端发送了 `Keep Alive` 但未能正确读取客户端的 `Keep Alive` 回包，20 秒倒计时一到，客户端就会显示红字 `Timed out` 并主动断开连接。
  * **解决办法**：使用 `asio::steady_timer` 在 Session 中挂载周期任务（建议 10 秒一次，20 秒未收到应答即主动断开）。
* ⚠️ **Keep Alive 载荷类型写错（1.12.2 高发）**：
  * 1.12.2 的 `Keep Alive` 载荷是 **`Long`（8 字节）**：服务端下发 `0x1F`，客户端应答 `0x0B`。若按 1.8 的习惯写成 `VarInt`，客户端会在解码时抛 `DecoderException`，表现为"进服后几十秒内莫名断开，且服务端日志毫无异常"。

---

### 阶段 3：区块内存模型与超平坦地形下发（Chunk & World Sync）

#### 1. 目标定义（Definition of Done）
* 玩家进服后，屏幕不再是空空如也的虚空。
* 脚下生成平整的方块地面（基岩、泥土、草方块），阳光普照，玩家可以在草地上正常跑步、跳跃，按 F3 能够看到当前所处的区块坐标（Chunk X, Z）。

#### 2. 区块与世界内存模型抽象

在 Minecraft 中，世界被划分为网格状的垂直柱状区块（Chunk）：
* **Chunk**：$16 \times 256 \times 16$ 个方块。
* **ChunkSection**：Chunk 在纵向上细分为 16 个小立方体，每个尺寸为 $16 \times 16 \times 16$（共 4096 个方块）。

```
        Y=256 ┌──────────────────┐
              │ ChunkSection 15  │ (16x16x16)
              ├──────────────────┤
              │      ......      │
              ├──────────────────┤
              │ ChunkSection 1   │ (16x16x16)
              ├──────────────────┤
         Y=0  │ ChunkSection 0   │ (16x16x16) --> 超平坦地面在这里 (基岩/泥土/草方块)
              └──────────────────┘
                 16 x 16 (X, Z)
```

#### 3. 任务拆解与设计契约

1. **ChunkSection 内存组织**：
   * 方块状态存储：每个方块用一个 **全局调色板 state id**（`(blockId << 4) | metadata`，1.12.2 中占 13 位）表示，用 `uint16_t` 存正好合适。
   * 内存表示可用一维数组：`uint16_t blocks[4096]`。
   * 空间局部性寻址公式（极度关键，1.12.2 与 1.8 完全一致）：
     `Index = (y << 8) | (z << 4) | x`，其中 $x, z \in [0, 15], y \in [0, 15]$。
2. **生成简单的超平坦地形**：
   * 只填充 `ChunkSection 0`，写入的是 **全局调色板 state id**：
     * $Y=0$：基岩（Bedrock，`blockId=7` → state id `(7<<4)|0 = 112`）
     * $Y=1 \sim 2$：泥土（Dirt，`blockId=3` → state id `48`）
     * $Y=3$：草方块（Grass Block，`blockId=2` → state id `32`）
     * $Y \ge 4$：空气（Air，state id `0`）
   * 把 `state_id = (id << 4) | meta` 封装成一个独立函数（如 `to_global_palette_state()`），**绝不要在生成器里散落硬编码数字**——Mojang 已声明未来会重构全局调色板，硬编码会让升级变成噩梦。
3. **序列化 Chunk Data (0x20) 包结构（1.12.2 全新格式，与 1.8 完全不同）**：
   * `Chunk X`: `Int`，`Chunk Z`: `Int`
   * `Ground-Up Continuous`: `bool`（新加载区块填 `true`）
   * `Primary Bit Mask`: **`VarInt`**（表示哪些 Section 有数据；只填 Section 0 时就是 `0x01`。注意 1.8 是 u16，1.12.2 是 VarInt）
   * `Size`: `VarInt`（后续 `Data` 字节流的长度，**必须与实际写入字节数严格相等**）
   * `Data`: 字节流，按顺序拼接：
     1. **每个激活 Section 的完整结构**（自底向上，数量 == Bit Mask 中 1 的个数）：
        1. `Bits Per Block`: `uint8`（本项目固定填 `13`，表示使用**直接全局调色板**）
        2. `Dummy Palette Length`: `VarInt`（直接模式下**恒为 `0`**）
        3. `Data Array Length`: `VarInt`（long 个数，13 位时恒为 `832`）
        4. `Data Array`: `Long[832]`，**每个 long 按大端 8 字节写入**；每个方块占 13 位，低位在前，**允许一个条目跨越两个 long**
        5. `Block Light`: `Byte[2048]`（每方块半字节，全填 `0x00`）
        6. `Sky Light`: `Byte[2048]`（每方块半字节，全填 `0xFF` 代表最亮日光；**仅主世界维度发送，地狱/末地不发**）
     2. 若 `Ground-Up Continuous` 为 `true`，末尾附加 **256 字节**的 Biome 数组（索引 `z * 16 | x`，填 `1` 代表平原 Plains，不支持群系时推荐填 `127` Void）
   * 单 Section（13 位直接调色板）的字节数是一个可用来做单测的"魔数"：
     `1 + 1 + 2 + 832 × 8 + 2048 + 2048 = 10756` 字节。
   * **位压缩核心算法（13 位/方块，可跨 long）**：
     ```cpp
     constexpr int kBits = 13, kBlocks = 4096, kLongs = kBlocks * kBits / 64; // 832
     uint64_t data[kLongs] = {0};
     const int idx = (y << 8) | (z << 4) | x;                 // 与内存寻址公式一致
     const uint64_t v = static_cast<uint64_t>(state_id) & ((1u << kBits) - 1);
     const int startLong = (idx * kBits) / 64, startOff = (idx * kBits) % 64;
     const int endLong   = ((idx + 1) * kBits - 1) / 64;
     data[startLong] |= v << startOff;
     if (startLong != endLong) data[endLong] |= v >> (64 - startOff);  // 跨界拆分
     ```
     （4096 × 13 ÷ 64 恰好整除，**不需要任何填充**；"不跨界 + 填充"是 1.16 才引入的规则，别提前实现错。）
4. **出生点周围区块推送**：
   * 玩家出生在 $(0, 0)$ 时，循环发送以 $(0, 0)$ 为中心、半径为 2 的区块矩阵（即 $X \in [-2, 2], Z \in [-2, 2]$，共 25 个区块）。
   * 客户端**不会渲染缺少邻居的区块**：只发一个孤立方块柱会导致"能交互但看不见"，所以必须成片下发。
   * 若之后要覆盖已加载的区块，**必须先发 `Unload Chunk (0x1D)`**（`Chunk X: Int` + `Chunk Z: Int`），否则客户端 chunk 缓存会泄漏。

#### 4. 易错点与避坑指南
* ⚠️ **负数坐标取整陷阱（导致区块错位）**：
  * 世界坐标转换到区块坐标的数学定义是向下取整（Floor）：
    $ChunkX = \lfloor X / 16 \rfloor$。
  * 在 C/C++ 中，对于负数，整数除法 `/` 是**向零截断**的！例如 `-1 / 16 == 0`，但实际它应该属于区块 `-1`！
  * **正确做法**：使用位移运算（在算术右移下）：`int chunk_x = x >> 4;`。这能够天然满足负数向下取整的要求。
* ⚠️ **把 1.8 的"每方块 2 字节小端"写法带到 1.12.2（最致命的迁移错误）**：
  * 1.12.2 的方块数据是**调色板 + Long 位压缩数组**，并且 Long 是**大端**。若沿用旧格式，客户端会直接抛解码异常或把光照数据当方块读，表现为满屏乱方块/闪退。
  * 请把 `doc/03_protocol_340_cheatsheet.md` §6 的算法当作唯一实现依据。
* ⚠️ **客户端闪退抛出 `ArrayIndexOutOfBoundsException`**：
  * 客户端解析区块包时，完全依赖 `Primary Bit Mask` 中 `1` 的数量来推算后续数据流的长度。
  * 如果你的 BitMask 声称有 2 个 Section，但实际字节流只写了 1 个 Section 的数据，客户端解析到末尾数组越界，整个 Minecraft 进程会当场崩溃闪退。
* ⚠️ **光照数组填错值会"污染"方块数据**：
  * 光照是每方块半字节：**偶数 x 塞低半字节，奇数 x 塞高半字节**，遍历时 `x += 2` 并写 `light[x] | (light[x+1] << 4)`。
  * 若你把地下的天空光也填 `0xFF`，玩家会看到"洞穴里也阳光普照"；若把 block light 填成非 0，会把区块读成发光的乱码。
* ⚠️ **对已加载区块重复发送 `Ground-Up Continuous = true`**：
  * 会造成客户端 chunk 缓存泄漏（F3 里两个 MultiplayerChunkCache 数值对不上）。重发前先发 `Unload Chunk (0x1D)`。
* ⚠️ **`Primary Bit Mask` 与 `Size` 必须自洽**：
  * 建议在序列化函数出口加一条断言：`size == sections.size() * 10756 + 256`（单 section、主世界、ground-up 场景），这是最省时间的自检手段。

---

### 阶段 4：并发解耦、20 TPS 节拍器与 AOI 视距广播

#### 1. 目标定义（Definition of Done）
* 启动服务器，打开两个独立的 Minecraft 客户端，均登录进服。
* 客户端 A 移动，客户端 B 能实时看到 A 的人物模型在跑动、转头。
* 客户端 A 敲击方块破坏地面，客户端 B 屏幕上的对应方块立刻消失；放置方块同理。

#### 2. 线程解耦与无锁队列设计

```
[ Asio 网络线程池 ]                      [ World 逻辑主线程 ]
  Session A 收到移动包 ──┐
  Session B 收到挖掘包 ──┼──► [ MPSC 无锁任务队列 ] ──► 批量 Pop 事件
  Session C 收到聊天包 ──┘                              │
                                                        ▼
                                                 执行 Tick 逻辑:
                                                 1. 修改 World 状态
                                                 2. 计算 AOI 观察者
                                                 3. 向周围 Session 投递下发包
```

#### 3. 任务拆解与设计契约

1. **无锁多生产者单消费者（MPSC）事件队列**：
   * 逻辑主线程负责唯一写所有游戏状态，彻底杜绝数据竞态。
   * 网络线程接收到客户端完整的 Packet 后，组装为一个包含 `session_id` 和 `packet` 的任务对象，`push` 进队列。
   * 可以使用单链表原子指针实现无锁 MPSC，或者在第一阶段先使用 `std::mutex` + 双缓冲队列（Double Buffering：一个读队列一个写队列，交换指针实现极低锁竞争）。
2. **精准 20 TPS（50ms）节拍器与时间漂移消除**：
   * 游戏每一刻（Tick）必须是精准的 50ms。
   * **绝对不能这么写**：
     ```cpp
     // 错误示范：会导致严重的累积时钟漂移！
     while (running) {
         tick();
         std::this_thread::sleep_for(50ms); // 忽略了 tick() 本身消耗的时间
     }
     ```
   * **正确设计逻辑**：记录下一次 Tick 的绝对时间点（`next_tick_time`）。每轮循环计算当前时间距离目标时间点的剩余空隙进行睡眠；若已经超时（Tick 掉帧），则立即补跑逻辑并报警。
3. **空间索引与 AOI（Area of Interest）视野广播**：
   * 维护一个二维哈希表：`std::unordered_map<ChunkPos, std::unordered_set<PlayerID>>`。
   * 当玩家从 `Chunk A` 移动到 `Chunk B` 时：
     * 计算玩家在 `Chunk A` 时的九宫格区块集合 $Set_A$。
     * 计算玩家在 `Chunk B` 时的九宫格区块集合 $Set_B$。
     * 差集 $Set_B - Set_A$：新看见的区块内的其他玩家，**先补发其 `Player List Item (0x2E, Action=0)`，再发 `Spawn Player (0x05)`**。
     * 差集 $Set_A - Set_B$：离开视野的玩家，向他们发送 `Destroy Entities (0x32)`。
     * 交集 $Set_A \cap Set_B$：原本就在视野内的玩家，发送相对位移包 `Entity Relative Move (0x26)`（或转头时用 `Entity Look And Relative Move (0x27)`）。
4. **方块交互同步**：
   * 监听客户端发来的 `Player Digging (0x14)`（当 `Status == 2` 时表示方块挖掘完成；`Location` 是 `Position` 位域打包坐标，`Face` 是 1 字节枚举）。
   * 监听 `Player Block Placement (0x1F)`（放置方块：`Location` + `Face` + `Hand` + 面内光标 `Float×3`；真正的落点坐标 = `Location` 沿 `Face` 法向量偏移一格）。
   * 主线程校验合法性后，修改 `World` 内存中对应的方块状态。
   * 向该坐标周围九宫格内的所有玩家广播 `Block Change (0x0B)` 数据包（`Location: Position` + `Block ID: VarInt` 全局调色板 state id）。
   * ⚠️ 创造模式下客户端会发 `Creative Inventory Action (0x1B)` 索取物品，服务端需回 `Set Slot (0x16)` 把方块放进快捷栏，否则玩家空手无法放置方块（`Player Block Placement` 会被服务端忽略）。

#### 4. 易错点与避坑指南
* ⚠️ **数据竞态（Data Race）与野指针**：
  * 新手最容易犯的错误：在 Asio 的 `async_read` 回调里直接去调 `world.get_player(id)->set_position(...)`。
  * Asio 运行在它的线程池里，World 运行在主线程里，这会直接造成多线程同时读写同一个内存结构，程序运行几分钟后必定随机出现莫名其妙的内存崩溃（SIGSEGV）。
  * **铁律**：网络线程只能做“解包”，解完后通过事件队列丢给主线程，主线程是修改游戏状态的唯一裁决者。
* ⚠️ **相对位移包的 8 方块溢出边界（1.12.2 已从 Byte 改为 Short）**：
  * 1.12.2 的 `Entity Relative Move (0x26)` 中，$\Delta X, \Delta Y, \Delta Z$ 使用 **`Short`（定点数，单位 $\frac{1}{4096}$ 方块）** 存储：`delta = (int16)((current - previous) * 4096)`。
  * 单轴最大跨度为 $32768 / 4096 = 8$ 个方块（1.8 是 Byte + $\frac{1}{32}$ 方块、上限 4 格，别照抄旧文档）。
  * 如果玩家发生了瞬移（传送）或者网络延迟后的大跨步位移，使用该包会导致坐标回弹错乱。一旦单轴移动距离 ≥ 8 方块，必须降级发送绝对坐标包 `Entity Teleport (0x4C)`。
  * 转头请用 `Entity Look And Relative Move (0x27)` / `Entity Look (0x28)`，其 `Yaw/Pitch` 是 **`Angle`（1 字节）且为绝对角度**，不是增量。
* ⚠️ **漏发 `Player List Item (0x2E)` 导致"人不见了"**：
  * `Spawn Player (0x05)` 必须**晚于**该玩家的 `Player List Item (0x2E, Action=0)` 发送，否则 Notchian 客户端不会生成实体——不报错、不闪退，只是"看不见人"，属于最难排查的一类 bug。
  * `Spawn Player` 里的 `Player UUID` 是 **16 字节原始 UUID**（不是 `Login Success` 里那种 36 字符字符串）。

---

### 阶段 5：压测机器人、性能 Profiling 与工业级调优

#### 1. 目标定义（Definition of Done）
* 编写一个脱离画面的纯 C++ 命令行轻量压测程序，能并发启动 500~1000 个虚拟玩家连接服务器，在地图上持续游走并发送坐标包。
* 主服务器在千人压测下，CPU 使用率健康，没有内存泄露，Tick 抖动维持在 $\pm 2$ms 范围。
* 产出火焰图（FlameGraph），明确指出瓶颈点并进行内存池优化。

#### 2. 任务拆解与性能调优步骤

1. **编写轻量级压测机器人（Headless Bot）**：
   * 仅包含基础网络层，不需要加载任何渲染和音效。
   * 机器人逻辑：
     * 建立 TCP 连接 -> 发送 Handshake（协议号 `340`）-> 发送 Login Start -> 接收 Login Success 和 Join Game。
     * 收到 `Player Position And Look (0x2F)` 后必须回 `Teleport Confirm (0x00)`，否则坐标状态会与服务端不一致。
     * 必须应答服务端的 `Keep Alive (0x1F)`：回一个同 ID 的 `Keep Alive (0x0B)`（**`Long` 载荷**），否则 20 秒内会被自己"假装的服务端"踢掉。
     * 启动一个定时器，以 20Hz（每 50ms 一次）随机向前方移动微小坐标，发送 `Player Position (0x0D)` 包（`X/Y/Z: Double` + `On Ground: Boolean`）。
     * 记得消费并丢弃服务端下发的 25 个 `Chunk Data (0x20)` 大包——**这部分解包/丢弃的 CPU 开销本身就是压测负载的一部分**，不要在 bot 里"跳过不读"，否则无法真实反映服务端的发送压力。
2. **使用 Linux `perf` 采样与火焰图绘制**：
   * 编译 Release 版本（带调试符号）：`-O2 -g -fno-omit-frame-pointer`。
   * 运行服务端并施加压测负载。
   * 采集 CPU 采样数据：
     ```bash
     perf record -F 99 -p $(pgrep main_app) -g -- sleep 30
     ```
   * 导出火焰图（使用开源 FlameGraph 工具）：
     ```bash
     perf script > out.perf
     ./stackcollapse-perf.pl out.perf > out.folded
     ./flamegraph.pl out.folded > flamegraph.svg
     ```
   * 在浏览器中打开 `flamegraph.svg`，观察“高原”状的热点函数。
3. **定位与实施内存池优化（Buffer Pool）**：
   * 在高并发下，火焰图中通常有大片区域被 `malloc`、`free` 或 `std::vector::reserve` 占据。
   * **优化方案**：针对网络收发高频使用的临时数据包，实现一个基于空闲链表（Free-List）或 Slab 思想的固定块内存池。网络包销毁时归还池中，不再归还操作系统，消除频繁系统调用开销。

#### 3. 易错点与避坑指南
* ⚠️ **Linux 默认文件描述符限制（`ulimit -n`）**：
  * 当你压测启动到 1024 个连接时，客户端或服务端可能突然报错 `Too many open files`。
  * 这是 Linux 系统默认单个进程打开 fd 数量的软限制（通常是 1024）。在压测前需通过 `ulimit -n 65535` 提高上限。
* ⚠️ **压测端口耗尽与 TIME_WAIT 堆积**：
  * 压测程序频繁重启时，如果短时间内建立并关闭数千个连接，本地会堆积大量 `TIME_WAIT` 状态的 socket，导致报错 `Cannot assign requested address`。
  * 可通过在 Linux 内核开启 `net.ipv4.tcp_tw_reuse` 解决。

---

## 四、 权威学习资源与文档速查

### 4.1 协议标准与数据定义手册

> ⚠️ **wiki.vg 原站已彻底下线**（实测 `https://wiki.vg/` 与 `https://wiki.vg/Protocol` 均返回 TCP 连接被拒）。为避免再现"1.8.8 文档找不到"的窘境，**整站快照已随仓库留档**在 [`doc/reference/`](reference/README.md)，并提供机器可读的 1.12.2 协议/方块数据。**查阅请优先用本地副本**。

1. **本地全站存档**：[`doc/reference/wiki.vg/`](reference/README.md) —— 216 个页面（`.html` 保真可离线浏览 + `.txt` 可 grep），含出处 URL、抓取时间与 sha256 校验清单。协议正文见 [`reference/wiki.vg/Protocol.html`](reference/wiki.vg/Protocol.html)。
2. **[本地] 区块格式专页**：[`reference/wiki.vg/Chunk_Format.html`](reference/wiki.vg/Chunk_Format.html)  
   * 区块包 `Data` 字段的权威结构说明：调色板（Palette）、位压缩 Long 数组（Bits Per Block）、光照半字节打包、生物群系数组与 Ground-Up Continuous 语义。
3. **[本地] 数据类型**：[`reference/wiki.vg/Data_types.html`](reference/wiki.vg/Data_types.html)  
   * 规范了 VarInt、VarLong、Position（64位位域打包）、Angle、定点数等特殊数据类型的严格编码算法。
4. **[本地] 服务器列表 Ping**：[`reference/wiki.vg/Server_List_Ping.html`](reference/wiki.vg/Server_List_Ping.html)  
   * `Handshake → Request → Response → Ping → Pong` 全流程与 JSON 字段语义（含 `favicon`、`players.sample`）。
5. **机器可读交叉校验**：[`reference/minecraft-data/1.12.2/protocol.json`](reference/minecraft-data/1.12.2/protocol.json)、[`reference/minecraft-data/1.12/blocks.json`](reference/minecraft-data/1.12/blocks.json)  
   * 前者是协议定义的 JSON/YAML 版（可脚本化比对包 ID），后者是 1.12.2 的方块数字 ID ↔ 名称 ↔ metadata 变体表（实现区块序列化时查表用）。
6. **本项目速查表**：[`doc/03_protocol_340_cheatsheet.md`](03_protocol_340_cheatsheet.md) —— 从上述文档中抽取、核对后的包 ID 全表与字段/坑位清单，**写代码时看它比翻网页快 10 倍**。

### 4.2 推荐必读书籍与对应核心章节
* **《C++ Primer (第 5 版)》**：重点重温第 12 章（动态内存与智能指针 `shared_ptr` / `weak_ptr`）、第 13 章（拷贝控制与移动语义 `std::move`）。
* **《Effective Modern C++》（Scott Meyers 著）**：
  * Item 21：优先选用 `std::make_unique` 和 `std::make_shared` 而非直接 `new`。
  * Item 23：理解 `std::move` 与 `std::forward`。
* **《C++ 并发编程实战（第 2 版）》（Anthony Williams 著）**：
  * 第 4 章：同步并发操作（条件变量与未来量）。
  * 第 5 章：C++ 内存模型与原子操作（深入理解无锁队列的基石）。
  * 第 6 章：基于锁的并发数据结构设计。
* **《Linux 高性能服务器编程》（游双 著）**：
  * 第 8 章：高性能服务器程序框架（深入理解 Reactor 模式与有限状态机）。
  * 第 9 章：I/O 复用（深入理解 epoll 原理）。

---

## 五、 大型 C++ 工程能力自查清单（Checklist）

在开发每一个模块时，用以下标准审视你的代码，培养工业级编码素养：

- [ ] **所有权清晰吗？**：没有出现裸指针 `delete`。独占资源使用 `std::unique_ptr`，共享回调资源使用 `std::shared_ptr`。
- [ ] **避免循环引用了吗？**：如果类之间存在双向关联（如 Session 持有 Player，Player 又持有 Session），一方必须使用 `std::weak_ptr`，防止内存无法释放泄漏。
- [ ] **避免不必要的内存拷贝了吗？**：大对象传参是否遵循 `const T&`？不可变的大数据块移动是否使用了 `std::move`？
- [ ] **接口具有异常安全性与自卫性吗？**：解包函数是否对剩余长度做了充分校验？非法数据到来时是优雅报错掐线，还是直接让进程崩溃？
- [ ] **日志分级合理吗？**：高频逻辑（如每 50ms 的 Tick 内部）严禁打印无意义的 `info` 日志；网络关键状态流转打 `info`，异常打 `warn` 或 `error`。
- [ ] **编译无警告吗？**：项目构建是否开启了 `-Wall -Wextra`，并且做到零 Warning 通过？