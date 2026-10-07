# MCPE 核心技术疑难与架构深度解答 (QA 知识库)

> 本知识库收录了基于 C++23 / Asio 构建高性能 Minecraft 服务端过程中，沉淀的核心 C++ 语言特性、网络 I/O 架构、协议与状态机、空间数学算法及工业级工程实践疑难问答。
> 全套内容分类拆解为 5 大独立专题，各专题**按领域独立成册、各自独立编号（Q1 ~ Qn）**，便于模块化查阅与复习。

---

## 🏛️ 系统架构与专题分层映射

```mermaid
flowchart TD
    subgraph Client [客户端交互]
        MC[Minecraft 客户端 1.12.2 / Protocol 340]
    end

    subgraph NetworkLayer [网络通信层 (专题 01)]
        TS[TcpServer / TcpSession]
        Asio[Asio Event Loop / epoll_wait]
        Buf[Socket 接收缓冲 / C1000K 设计]
    end

    subgraph MemoryLang [现代 C++ 与内存架构 (专题 02)]
        SmartPtr[智能指针 / weak_ptr / 控制块机制]
        Alloc[jemalloc / mimalloc / 内存池对比]
        Move[移动语义 / 编译优化 / string_view]
    end

    subgraph ProtocolLayer [协议与业务状态机 (专题 03)]
        Framing[PacketFraming 拆包 / Cheap Prepend 组包]
        Router[PacketRouter 状态机 / 移动包放行]
        TwoPhase[teleport_id 两阶段确认握手]
    end

    subgraph WorldLayer [世界体系与空间数学 (专题 04)]
        Chunk[Chunk / ChunkSection 调色板切片]
        Spatial[ChunkPos 哈希 / 局部坐标 / 负数补码]
    end

    subgraph QAProcess [工程规范与测试保障 (专题 05)]
        DoD[敏捷 Sprint DoD 验收标准]
        Tests[GTest / CTest 182 项测试矩阵]
    end

    MC <-->|TCP 字节流| TS
    TS --- Asio
    TS --- Framing
    Framing --- Router
    Router --- TwoPhase
    Router <--> WorldLayer
    NetworkLayer -.-> MemoryLang
    ProtocolLayer -.-> MemoryLang
    QAProcess -.->|全覆盖护航| NetworkLayer
    QAProcess -.->|全覆盖护航| ProtocolLayer
    QAProcess -.->|全覆盖护航| WorldLayer
```

---

## 📚 专题分类矩阵

| 专题编号 | 专题文档 | 核心领域 | 包含题数 | 独立编号范围 |
| :--- | :--- | :--- | :---: | :---: |
| **专题 01** | [`01_network_asio.md`](./01_network_asio.md) | 网络通信与 Asio 异步 I/O | 16 题 | **Q1 ~ Q16** |
| **专题 02** | [`02_modern_cpp_memory.md`](./02_modern_cpp_memory.md) | 现代 C++ 语言特性与内存架构 | 10 题 | **Q1 ~ Q10** |
| **专题 03** | [`03_protocol_state_machine.md`](./03_protocol_state_machine.md) | 通信协议、数据切片与业务状态机 | 12 题 | **Q1 ~ Q12** |
| **专题 04** | [`04_world_spatial_math.md`](./04_world_spatial_math.md) | 世界体系、空间数学与区块系统 | 7 题 | **Q1 ~ Q7** |
| **专题 05** | [`05_engineering_testing.md`](./05_engineering_testing.md) | 敏捷工程、架构思维与质量保证 | 5 题 | **Q1 ~ Q5** |

---

## 🏷️ 高频技术专题与面试考点速查

- **Asio 异步事件驱动与 I/O 多路复用**：
  - `TcpSession` 异步自保与 `enable_shared_from_this`：[专题 01 (Q1)](./01_network_asio.md#q1-tcpservercc-中为什么要用-stdmake_sharedtcpsessionstdmovesocket), [专题 01 (Q11)](./01_network_asio.md#q11-tcpsessiondoread-中的异步-lambda-捕获列表-this-self-可以改成-this-self-吗), [专题 02 (Q4)](./02_modern_cpp_memory.md#q4-tcpsession-为什么要继承-stdenable_shared_from_thispacketrouterdispatch-参数为什么是-shared_ptr-而不是引用)
  - epoll_wait 中断唤醒与底层模型：[专题 01 (Q3)](./01_network_asio.md#q3-asio-异步接口底层模型剖析谁在休眠网卡中断与回调触发的完整链路是怎样的), [专题 01 (Q5)](./01_network_asio.md#q5-数据未到时线程在-epoll_wait-休眠数据到了从它恢复对吗休眠时间有上限吗), [专题 01 (Q7)](./01_network_asio.md#q7-底层是类似-while-epoll_wait-的循环吗为什么这里可以-while-而业务代码不能)
  - Proactor 模拟机制与 Reactor 对比：[专题 01 (Q4)](./01_network_asio.md#q4-不是说-asio-是-proactor-模型吗为什么上一题提到了-epollreactor), [专题 01 (Q8)](./01_network_asio.md#q8-以上讨论都是异步模式无事就休眠那同步-io-呢同步也休眠吗两者的本质区别是什么)
  - C100K/C1000K 极致内存优化与 Socket 接收缓冲：[专题 01 (Q12)](./01_network_asio.md#q12-async_read_some-会怎么处理传入的缓冲区如-read_buffer_单次读满-4kb-会怎么办), [专题 01 (Q13)](./01_network_asio.md#q13-既然已经有了能动态扩容的-cumulative_buf_为什么很多网络库还要加一层固定大小的-read_buffer_-中转), [专题 01 (Q14)](./01_network_asio.md#q14-当前实现中-read_buffer_-不也是每个连接-4kb-吗中转写法有什么硬伤如何改成零拷贝直读), [专题 01 (Q15)](./01_network_asio.md#q15-在海量高并发c100kc1000k场景下工业级真正极度省内存的-socket-接收缓冲应该怎么设计)
  - 异步写线程安全与 `asio::post` 队列串行化：[专题 01 (Q9)](./01_network_asio.md#q9-task-13-中为什么-sendpacketbytebuffer-buf-只有一个参数数据已经在-buf-中了吗async_write-会做什么), [专题 01 (Q10)](./01_network_asio.md#q10-为什么-tcpsessionsendpacket-要用-asiopost这个-post-该如何理解)
- **现代 C++ 语言特性与内存管理**：
  - 移动语义值传递 (Pass-by-value) 与参数优化：[专题 02 (Q1)](./02_modern_cpp_memory.md#q1-为什么-stdmove--传值可以避免拷贝stdmove--传引用const--或-不行吗), [专题 02 (Q6)](./02_modern_cpp_memory.md#q6-解析数据包字段时为什么-const-stdstring-host-用常引用而-int32_t-和-uint16_t-却直接按值拷贝)
  - 智能指针所有权哲学与弱指针 `weak_ptr` 控制块双计数：[专题 02 (Q8)](./02_modern_cpp_memory.md#q8-loginhandler-调用-playhandleronplayerjoinsession-为什么不用增加引用计数到底什么时候应该增加计数), [专题 02 (Q10)](./02_modern_cpp_memory.md#q10-深入现代-c-智能指针什么是-stdweak_ptr底层控制块原理是什么在工业级架构中为什么以及何时必须使用它)
  - 工业级内存分配器深度横评 (Jemalloc / TCMalloc / mimalloc / Folly)：[专题 02 (Q9)](./02_modern_cpp_memory.md#q9-mcpe-内存池性能实测当前实现最像哪个经典实现与-follymimalloc-基准对比表现如何存在哪些工业级差距)
  - `string_view` 零开销与只读数据段生命周期：[专题 02 (Q2)](./02_modern_cpp_memory.md#q2-constexpr-stdstring_view-tostringconnectionstate-state-为什么返回-string_view对比返回-string-有何优势), [专题 02 (Q3)](./02_modern_cpp_memory.md#q3-return-handshaking-不是临时构造吗为什么永生是因为-constexpr-吗)
- **协议封包、切片与业务状态机**：
  - VarInt 变长整数位运算与分层作用域设计：[专题 03 (Q2)](./03_protocol_state_machine.md#q2-bytebufferh-中的-kvaluemask-和-kcontinuationbit-放在类内更好还是-mcpe-命名空间更好有其他方案吗), [专题 03 (Q3)](./03_protocol_state_machine.md#q3-packetframing-拆包时不是也需要用到-kcontinuationbit-0x80-吗为什么会有这种代码如何优雅解决)
  - 零拷贝组包 (Cheap Prepend) 与拆包切片安全性：[专题 03 (Q7)](./03_protocol_state_machine.md#q7-构造数据包时为什么要先写内层-body-再封装进外层-packet为什么不能直接从外往里写如何利用-prepend-零拷贝封装), [专题 03 (Q8)](./03_protocol_state_machine.md#q8-packetframingslicepacket-拆包时将数据拷贝进新-bytebuffer-是必要的吗能否实现零拷贝zero-copy)
  - 有限状态机 (FSM) 架构与库选型：[专题 03 (Q4)](./03_protocol_state_machine.md#q4-task-15-中什么叫状态机为什么-minecraft-协议必须用状态机为什么有人说-boostmsm-状态机实现最优雅), [专题 03 (Q5)](./03_protocol_state_machine.md#q5-那什么项目适合-boostmsm-或-boost-extsml现代工业界在什么场景下会使用状态机库)
  - 传送流水号 `teleport_id` 两阶段确认与反作弊防幽灵拉回：[专题 03 (Q10)](./03_protocol_state_machine.md#q10-真实客户端联调实测复盘为什么客户端刚确认-teleport-id-0x00-就瞬间掉线protocol-340-移动包-0x0e-机制与路由防御反思), [专题 03 (Q12)](./03_protocol_state_machine.md#q12-什么是-teleport_id传送流水号为什么需要它底层两阶段确认机制是如何防御网络幽灵拉回与竞态的)
  - 实体 ID (VarInt) 与 UUID (128位) 选型对比：[专题 03 (Q11)](./03_protocol_state_machine.md#q11-什么是-entity-id实体-id它与方块有何区别既然已经有了-uuid为什么协议层还必须使用-entity-id)
- **世界体系、空间数学与体素算法**：
  - 区块柱体与 ChunkSection 结构：[专题 04 (Q1)](./04_world_spatial_math.md#q1-什么是-chunk区块为什么世界三维立体而-chunkpos-却只有-x-和-z-两个维度)
  - 素数 3129871 哈希散列与对角线碰撞防御：[专题 04 (Q2)](./04_world_spatial_math.md#q2-剖析-stdhashmcpeworldchunkpos-哈希特化实现为什么不能直接-x--z素数乘法-3129871-起了什么作用)
  - 负数补码算术右移与区块局部坐标：[专题 04 (Q3)](./04_world_spatial_math.md#q3-什么是-localx--localz区块局部坐标为什么需要它x--0x0f-位运算背后的负数补码原理是什么)
  - 区块调色板（Palette）与全局直接模式：[专题 04 (Q4)](./04_world_spatial_math.md#q4-什么是区块调色板palette为什么-bitsperblock--13-时-palette-length-固定为-varint-0全局调色板直接模式)
  - 方块光与天空光 2048 字节 Nibble 压缩与防黑屏：[专题 04 (Q5)](./04_world_spatial_math.md#q5-为什么-chunksection-中-block-light-和-sky-light-各占-2048-字节为什么方块光填-0x00-而天空光填-0xff)
  - ground_up 全量/增量语义与 Biomes 256 字节渲染机制：[专题 04 (Q6)](./04_world_spatial_math.md#q6-chunk-数据包中-ground_up-标志位是什么语义biomes_-数组存储的是什么以及客户端如何使用)
  - 连续 13 位跨 Long 压缩与位掩码防比特渗漏：[专题 04 (Q7)](./04_world_spatial_math.md#q7-packtobits13-中为什么要进行---1ull--kbitsperblock---1-位与运算如果省略会引发什么灾难)
- **敏捷工程管理与测试体系**：
  - 自底向上 Sprint 任务划分与驱动：[专题 05 (Q1)](./05_engineering_testing.md#q1-为什么-sprint-任务划分是从下往上bottom-up的觉得无从下手从上往下top-down会不会更好)
  - 企业级测试金字塔与 DoD 交付标准：[专题 05 (Q4)](./05_engineering_testing.md#q4-一个优秀的测试程序该怎么写如何根据-sprint-的-dod-写出高质量测试企业实际开发中的测试体系是怎样的)
  - GTest / CTest 框架与测试驱动开发：[专题 05 (Q2)](./05_engineering_testing.md#q2-tests-目录下的自动化测试程序在测什么gtest-核心机制如何理解为什么跑测试会有-1-个报错), [专题 05 (Q3)](./05_engineering_testing.md#q3-vs-code-运行测试时控制台显示-100-tests-passed-0-tests-failed-out-of-1-是怎么回事为什么在末尾看不到汇总)
  - 高性能系统与热路径日志决策准则：[专题 05 (Q5)](./05_engineering_testing.md#q5-chunksetblockgetblock-坐标越界为什么不记日志高性能系统与游戏引擎中如何判断某个地方要不要加日志)

---

## 📑 各专题完整问答导航

### 专题 01：[网络通信与 Asio 异步 I/O (Network & Asio I/O)](./01_network_asio.md)

> 基于 Asio 的异步网络编程模型，涵盖事件循环（Event Loop）、epoll_wait 休眠唤醒链路、Proactor/Reactor 模式对比、单连接缓冲与 C1000K 海量高并发省内存架构设计。

- [Q1: `TcpServer.cc` 中为什么要用 `std::make_shared<TcpSession>(std::move(socket))`？](./01_network_asio.md#q1-tcpservercc-中为什么要用-stdmake_sharedtcpsessionstdmovesocket)
- [Q2: `acceptor_.async_accept` 回调中的 `doAccept()` 这种写法叫什么？是递归吗？](./01_network_asio.md#q2-acceptor_async_accept-回调中的-doaccept-这种写法叫什么是递归吗)
- [Q3: Asio 异步接口底层模型剖析：谁在休眠？网卡中断与回调触发的完整链路是怎样的？](./01_network_asio.md#q3-asio-异步接口底层模型剖析谁在休眠网卡中断与回调触发的完整链路是怎样的)
- [Q4: 不是说 Asio 是 Proactor 模型吗？为什么上一题提到了 epoll（Reactor）？](./01_network_asio.md#q4-不是说-asio-是-proactor-模型吗为什么上一题提到了-epollreactor)
- [Q5: 数据未到时线程在 epoll_wait 休眠、数据到了从它恢复对吗？休眠时间有上限吗？](./01_network_asio.md#q5-数据未到时线程在-epoll_wait-休眠数据到了从它恢复对吗休眠时间有上限吗)
- [Q6: epoll_wait 超时后还会执行我传入的回调函数吗？Asio 是怎么处理的？](./01_network_asio.md#q6-epoll_wait-超时后还会执行我传入的回调函数吗asio-是怎么处理的)
- [Q7: 底层是类似 while() { epoll_wait(); } 的循环吗？为什么这里可以 while 而业务代码不能？](./01_network_asio.md#q7-底层是类似-while-epoll_wait-的循环吗为什么这里可以-while-而业务代码不能)
- [Q8: 以上讨论都是异步模式（无事就休眠），那同步 I/O 呢？同步也休眠吗？两者的本质区别是什么？](./01_network_asio.md#q8-以上讨论都是异步模式无事就休眠那同步-io-呢同步也休眠吗两者的本质区别是什么)
- [Q9: TASK-1.3 中为什么 sendPacket(ByteBuffer buf) 只有一个参数？数据已经在 buf 中了吗？async_write 会做什么？](./01_network_asio.md#q9-task-13-中为什么-sendpacketbytebuffer-buf-只有一个参数数据已经在-buf-中了吗async_write-会做什么)
- [Q10: 为什么 TcpSession::sendPacket 要用 asio::post？这个 post 该如何理解？](./01_network_asio.md#q10-为什么-tcpsessionsendpacket-要用-asiopost这个-post-该如何理解)
- [Q11: `TcpSession::doRead()` 中的异步 Lambda 捕获列表 `[this, self]` 可以改成 `[this, &self]` 吗？](./01_network_asio.md#q11-tcpsessiondoread-中的异步-lambda-捕获列表-this-self-可以改成-this-self-吗)
- [Q12: async_read_some 会怎么处理传入的缓冲区（如 read_buffer_）？单次读满 4KB 会怎么办？](./01_network_asio.md#q12-async_read_some-会怎么处理传入的缓冲区如-read_buffer_单次读满-4kb-会怎么办)
- [Q13: 既然已经有了能动态扩容的 cumulative_buf_，为什么很多网络库还要加一层固定大小的 read_buffer_ 中转？](./01_network_asio.md#q13-既然已经有了能动态扩容的-cumulative_buf_为什么很多网络库还要加一层固定大小的-read_buffer_-中转)
- [Q14: 当前实现中 read_buffer_ 不也是每个连接 4KB 吗？中转写法有什么硬伤？如何改成零拷贝直读？](./01_network_asio.md#q14-当前实现中-read_buffer_-不也是每个连接-4kb-吗中转写法有什么硬伤如何改成零拷贝直读)
- [Q15: 在海量高并发（C100K/C1000K）场景下，工业级“真正极度省内存”的 Socket 接收缓冲应该怎么设计？](./01_network_asio.md#q15-在海量高并发c100kc1000k场景下工业级真正极度省内存的-socket-接收缓冲应该怎么设计)
- [Q16: TcpServer 连接建立时 session_factory_、callbacks_ 和 packet_handler_ 三个分支分别代表什么语义？架构上如何考量？](./01_network_asio.md#q16-tcpserver-连接建立时-session_factory_callbacks_-和-packet_handler_-三个分支分别代表什么语义架构上如何考量)

### 专题 02：[现代 C++ 语言特性与内存架构 (Modern C++ & Memory Architecture)](./02_modern_cpp_memory.md)

> 现代 C++ (C++11 ~ C++23) 在工程实战中的关键语言特性与内存设计，包括移动语义值传递、智能指针控制块与循环引用解耦、`string_view` 零分配、Jemalloc / TCMalloc / mimalloc 内存池深度对比与基准评测。

- [Q1: 为什么 std::move + 传值可以避免拷贝？std::move + 传引用（const & 或 &）不行吗？](./02_modern_cpp_memory.md#q1-为什么-stdmove--传值可以避免拷贝stdmove--传引用const--或-不行吗)
- [Q2: constexpr std::string_view toString(ConnectionState state) 为什么返回 string_view？对比返回 string 有何优势？](./02_modern_cpp_memory.md#q2-constexpr-stdstring_view-tostringconnectionstate-state-为什么返回-string_view对比返回-string-有何优势)
- [Q3: return "HANDSHAKING" 不是临时构造吗？为什么永生？是因为 constexpr 吗？](./02_modern_cpp_memory.md#q3-return-handshaking-不是临时构造吗为什么永生是因为-constexpr-吗)
- [Q4: TcpSession 为什么要继承 std::enable_shared_from_this？PacketRouter::dispatch 参数为什么是 shared_ptr 而不是引用？](./02_modern_cpp_memory.md#q4-tcpsession-为什么要继承-stdenable_shared_from_thispacketrouterdispatch-参数为什么是-shared_ptr-而不是引用)
- [Q5: StatusHandler.h 中为什么 TcpSession 用前置声明而 ByteBuffer 直接 #include？](./02_modern_cpp_memory.md#q5-statushandlerh-中为什么-tcpsession-用前置声明而-bytebuffer-直接-include)
- [Q6: 解析数据包字段时，为什么 `const std::string& host` 用常引用，而 `int32_t` 和 `uint16_t` 却直接按值拷贝？](./02_modern_cpp_memory.md#q6-解析数据包字段时为什么-const-stdstring-host-用常引用而-int32_t-和-uint16_t-却直接按值拷贝)
- [Q7: `size_t` 隐式转 `int32_t` 为什么不推荐？如何优雅避免 `static_cast`？`assert` 中 `&& "字符串"` 的黑科技是什么原理？](./02_modern_cpp_memory.md#q7-sizet-隐式转-int32t-为什么不推荐如何优雅避免-static_castassert-中--字符串-的黑科技是什么原理)
- [Q8: LoginHandler 调用 PlayHandler::onPlayerJoin(session) 为什么不用增加引用计数？到底什么时候应该增加计数？](./02_modern_cpp_memory.md#q8-loginhandler-调用-playhandleronplayerjoinsession-为什么不用增加引用计数到底什么时候应该增加计数)
- [Q9: mcpe 内存池性能实测：当前实现最像哪个经典实现？与 Folly、mimalloc 基准对比表现如何？存在哪些工业级差距？](./02_modern_cpp_memory.md#q9-mcpe-内存池性能实测当前实现最像哪个经典实现与-follymimalloc-基准对比表现如何存在哪些工业级差距)
- [Q10: 深入现代 C++ 智能指针：什么是 std::weak_ptr？底层控制块原理是什么？在工业级架构中为什么以及何时必须使用它？](./02_modern_cpp_memory.md#q10-深入现代-c-智能指针什么是-stdweak_ptr底层控制块原理是什么在工业级架构中为什么以及何时必须使用它)
- [Q11: 现代 C++ 模式匹配黑科技：Overloaded 模式底层原理是什么？为什么必须写 using Ts::operator()... 与 CTAD 推导指引？](./02_modern_cpp_memory.md#q11-现代-c-模式匹配黑科技overloaded-模式底层原理是什么为什么必须写-using-tsoperator-与-ctad-推导指引)

### 专题 03：[通信协议、数据切片与业务状态机 (Protocol, Framing & State Machine)](./03_protocol_state_machine.md)

> Minecraft 协议封包与拆包机制、VarInt 编解码、Cheap Prepend 预留前向缓冲区零拷贝设计、有限状态机（FSM）架构选型、实机联调掉线复盘，以及防网络幽灵拉回的 teleport_id 两阶段确认握手机制。

- [Q1: PacketFraming::slicePacket 中为什么 packet_len <= 0 也返回 kPacketTooLarge？](./03_protocol_state_machine.md#q1-packetframingslicepacket-中为什么-packet_len--0-也返回-kpackettoolarge)
- [Q2: ByteBuffer.h 中的 kValueMask 和 kContinuationBit 放在类内更好还是 mcpe 命名空间更好？有其他方案吗？](./03_protocol_state_machine.md#q2-bytebufferh-中的-kvaluemask-和-kcontinuationbit-放在类内更好还是-mcpe-命名空间更好有其他方案吗)
- [Q3: PacketFraming 拆包时不是也需要用到 kContinuationBit (0x80) 吗？为什么会有这种代码？如何优雅解决？](./03_protocol_state_machine.md#q3-packetframing-拆包时不是也需要用到-kcontinuationbit-0x80-吗为什么会有这种代码如何优雅解决)
- [Q4: TASK-1.5 中什么叫状态机？为什么 Minecraft 协议必须用状态机？为什么有人说 Boost.MSM 状态机实现最优雅？](./03_protocol_state_machine.md#q4-task-15-中什么叫状态机为什么-minecraft-协议必须用状态机为什么有人说-boostmsm-状态机实现最优雅)
- [Q5: 那什么项目适合 Boost.MSM 或 boost-ext/sml？现代工业界在什么场景下会使用状态机库？](./03_protocol_state_machine.md#q5-那什么项目适合-boostmsm-或-boost-extsml现代工业界在什么场景下会使用状态机库)
- [Q6: 组装 Status Response 为什么用 `std::format` 字符串模板而不用三方 JSON 库？后续开发还用不用 JSON 库？](./03_protocol_state_machine.md#q6-组装-status-response-为什么用-stdformat-字符串模板而不用三方-json-库后续开发还用不用-json-库)
- [Q7: 构造数据包时为什么要先写内层 `body` 再封装进外层 `packet`？为什么不能直接从外往里写？如何利用 `prepend` 零拷贝封装？](./03_protocol_state_machine.md#q7-构造数据包时为什么要先写内层-body-再封装进外层-packet为什么不能直接从外往里写如何利用-prepend-零拷贝封装)
- [Q8: `PacketFraming::slicePacket` 拆包时将数据拷贝进新 `ByteBuffer` 是必要的吗？能否实现零拷贝（Zero-Copy）？](./03_protocol_state_machine.md#q8-packetframingslicepacket-拆包时将数据拷贝进新-bytebuffer-是必要的吗能否实现零拷贝zero-copy)
- [Q9: Minecraft 客户端与服务端交互的所有数据包都是「包长 + ID + 载荷」格式吗？](./03_protocol_state_machine.md#q9-minecraft-客户端与服务端交互的所有数据包都是包长--id--载荷格式吗)
- [Q10: 真实客户端联调实测复盘：为什么客户端刚确认 Teleport ID (0x00) 就瞬间掉线？Protocol 340 移动包 (0x0E) 机制与路由防御反思](./03_protocol_state_machine.md#q10-真实客户端联调实测复盘为什么客户端刚确认-teleport-id-0x00-就瞬间掉线protocol-340-移动包-0x0e-机制与路由防御反思)
- [Q11: 什么是 Entity ID（实体 ID）？它与方块有何区别？既然已经有了 UUID，为什么协议层还必须使用 Entity ID？](./03_protocol_state_machine.md#q11-什么是-entity-id实体-id它与方块有何区别既然已经有了-uuid为什么协议层还必须使用-entity-id)
- [Q12: 什么是 teleport_id（传送流水号）？为什么需要它？底层两阶段确认机制是如何防御网络幽灵拉回与竞态的？](./03_protocol_state_machine.md#q12-什么是-teleport_id传送流水号为什么需要它底层两阶段确认机制是如何防御网络幽灵拉回与竞态的)

### 专题 04：[世界体系、空间数学与区块系统 (World System, Spatial Math & Chunk Protocol)](./04_world_spatial_math.md)

> Minecraft 三维世界与体素空间数学算法，详解 Chunk 二维柱体划分、ChunkPos 哈希特化与素数混淆雪崩效应、区块局部坐标与负数补码位运算原理。

- [Q1: 什么是 Chunk（区块）？为什么世界三维立体，而 ChunkPos 却只有 x 和 z 两个维度？](./04_world_spatial_math.md#q1-什么是-chunk区块为什么世界三维立体而-chunkpos-却只有-x-和-z-两个维度)
- [Q2: 剖析 std::hash<mcpe::world::ChunkPos> 哈希特化实现：为什么不能直接 x ^ z？素数乘法 3129871 起了什么作用？](./04_world_spatial_math.md#q2-剖析-stdhashmcpeworldchunkpos-哈希特化实现为什么不能直接-x--z素数乘法-3129871-起了什么作用)
- [Q3: 什么是 localX / localZ（区块局部坐标）？为什么需要它？x & 0x0F 位运算背后的负数补码原理是什么？](./04_world_spatial_math.md#q3-什么是-localx--localz区块局部坐标为什么需要它x--0x0f-位运算背后的负数补码原理是什么)
- [Q4: 什么是区块调色板（Palette）？为什么 bitsPerBlock = 13 时 Palette Length 固定为 VarInt 0（全局调色板直接模式）？](./04_world_spatial_math.md#q4-什么是区块调色板palette为什么-bitsperblock--13-时-palette-length-固定为-varint-0全局调色板直接模式)
- [Q5: 为什么 ChunkSection 中 Block Light 和 Sky Light 各占 2048 字节？为什么方块光填 0x00 而天空光填 0xFF？](./04_world_spatial_math.md#q5-为什么-chunksection-中-block-light-和-sky-light-各占-2048-字节为什么方块光填-0x00-而天空光填-0xff)
- [Q6: Chunk 数据包中 ground_up 标志位是什么语义？biomes_ 数组存储的是什么以及客户端如何使用？](./04_world_spatial_math.md#q6-chunk-数据包中-ground_up-标志位是什么语义biomes_-数组存储的是什么以及客户端如何使用)
- [Q7: packToBits13 中为什么要进行 & ((1ULL << kBitsPerBlock) - 1) 位与运算？如果省略会引发什么灾难？](./04_world_spatial_math.md#q7-packtobits13-中为什么要进行---1ull--kbitsperblock---1-位与运算如果省略会引发什么灾难)

### 专题 05：[敏捷工程、架构思维与质量保证 (Engineering, Architecture & Testing)](./05_engineering_testing.md)

> 后端开发中的工程思维与质量保障规范，包括自底向上敏捷 Sprint 任务拆解、企业级测试金字塔与 DoD 交付标准、GTest/CTest 单元测试框架核心机制。

- [Q1: 为什么 Sprint 任务划分是从下往上（Bottom-Up）的？觉得无从下手，从上往下（Top-Down）会不会更好？](./05_engineering_testing.md#q1-为什么-sprint-任务划分是从下往上bottom-up的觉得无从下手从上往下top-down会不会更好)
- [Q2: `tests/` 目录下的自动化测试程序在测什么？GTest 核心机制如何理解？为什么跑测试会有 1 个报错？](./05_engineering_testing.md#q2-tests-目录下的自动化测试程序在测什么gtest-核心机制如何理解为什么跑测试会有-1-个报错)
- [Q3: VS Code 运行测试时控制台显示 `100% tests passed, 0 tests failed out of 1` 是怎么回事？为什么在末尾看不到汇总？](./05_engineering_testing.md#q3-vs-code-运行测试时控制台显示-100-tests-passed-0-tests-failed-out-of-1-是怎么回事为什么在末尾看不到汇总)
- [Q4: 一个优秀的测试程序该怎么写？如何根据 Sprint 的 DoD 写出高质量测试？企业实际开发中的测试体系是怎样的？](./05_engineering_testing.md#q4-一个优秀的测试程序该怎么写如何根据-sprint-的-dod-写出高质量测试企业实际开发中的测试体系是怎样的)
- [Q5: `Chunk::setBlock/getBlock` 坐标越界为什么不记日志？高性能系统与游戏引擎中如何判断某个地方要不要加日志？](./05_engineering_testing.md#q5-chunksetblockgetblock-坐标越界为什么不记日志高性能系统与游戏引擎中如何判断某个地方要不要加日志)

