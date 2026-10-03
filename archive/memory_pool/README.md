# 自定义无锁定长内存池归档目录 (Archived Memory Pool)

> **归档说明**：
> 本目录收录了项目前期自研的定长 4KB 无锁并发内存池系统（`BufferPool`、`ShardedBufferPool` 与 `SessionReadBuffer`）。
> 
> **归档决策背景**：
> 1. 项目仍处于初步开发阶段，过早引入深度定制的无锁内存池增加了系统的认知与维护心智负担；
> 2. 现代工业级多线程分配器（如 **jemalloc**）已具备世界顶级的线程局部缓存（Per-Thread Cache）、多阶规格池（Size Classes）、脏页自动衰减回收（Decay/Purging）能力，完全能够支撑当前阶段的超高吞吐网络需求；
> 3. 遵从“**先度量再优化（Measure before optimize）**”与“**避免过早优化（Premature optimization is the root of all evil）**”原则：
>    现阶段服务端主干直接链接并依托 **jemalloc** 驱动标准容器与网络缓冲区；后续若在全链路真实压测中证实此处成为特定瓶颈，可随时从本目录唤醒并重新接入自定义内存池。

---

## 一、 归档文件清单

* **核心池化组件**：
  - `src/common/BufferPool.h` / `.cc`：基于 Facebook Folly TaggedHead（32 位 Index + 32 位 Tag 单字 CAS 防 ABA）与 4MB 二级连续 Arena 动态扩容链的 4KB 定长无锁内存池。
  - `src/common/ShardedBufferPool.h` / `.cc`：基于 CPU 64 字节缓存行对齐（`alignas(64)`）的分片内存池，支持线程亲和映射与无锁跨分片工作窃取（Work Stealing）。
  - `src/network/SessionReadBuffer.h`：专为 `TcpSession` 设计的零常驻、惰性池化接收缓冲封装。
* **单元与压力测试集**：
  - `tests/test_buffer_pool.cc`：单池基本借还、ABA 免疫校验、16 线程疯狂争抢压测、4MB Chunk 动态连续挂载。
  - `tests/test_sharded_buffer_pool.cc`：分片借还、跨分片工作窃取、跨线程异核归还测试。
  - `tests/test_session_read_buffer.cc`：零常驻生命周期校验、半包碎片整理、畸形大包防护。
* **专业基准测试套件**：
  - `benchmarks/bench_buffer_pool.cc`：Google Benchmark 单线程、多线程与批处理基准。
  - `benchmarks/bench_crossthread.cc`：生产者-消费者跨线程 Larson 借还模型。
  - `benchmarks/bench_mempool_compare.cc`：对比传统互斥锁池、mcpe 单池/分片池与 DPDK 核心本地缓存池。

---

## 二、 若后续需要恢复启用，如何操作？

1. 将 `src/common/` 与 `src/network/` 下的相关源码拷回主干 `src/`；
2. 在 `CMakeLists.txt` 的 `mcpe_common` 库中重新添加 `src/common/BufferPool.cc` 与 `src/common/ShardedBufferPool.cc`；
3. 将 `src/common/ByteBuffer` 与 `src/network/TcpSession` 的内存托管逻辑重新切换为内存池借还模式；
4. 恢复 `tests/` 中的单元测试并在 `CMakeLists.txt` 中重新注册测试用例。
