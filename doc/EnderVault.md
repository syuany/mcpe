# EnderVault：基于 Go + gRPC 的分布式游戏数据中心与资产微服务

> **项目命名**：**EnderVault（末影宝库）** —— 取自 Minecraft **末影箱（Ender Chest）**“跨维度/跨空间共享且持久一致”的特性，隐喻分布式跨服玩家数据与资产持久化中心。  
> **工程架构**：作为 `mcpe` 项目的 **Monorepo（单体大仓）子服务**，源码存放于 `services/endervault/`，共享根目录下的跨语言 `proto/` 契约，支持随 `mcpe` 启动时自动伴随拉起与优雅停止。

该项目将与项目一（C++ 游戏网关 `mcpe`）完美联动，补齐简历在 **跨语言微服务通信、缓存架构（Redis）、数据库持久化（MySQL）及并发数据一致性** 上的核心技术点。

---

### 一、 技术选型、工程布局与协同拉起机制

#### 1. 技术栈选型
* **开发语言**：Go (Golang 1.20+)
  * *优势*：语法精简、内置原生高并发（Goroutine）、标准库完善、极易与 C++ 共享 Protobuf 协议。
* **开发耗时预估**：**2.5 周（约 18~20 天）**（前 3 天学习 Go 基础，后 2 周专注于工程实现与压测）。
* **核心技术栈选型**：
  * **RPC 通信**：`google.golang.org/grpc` + `google.golang.org/protobuf`
  * **数据库与 ORM**：MySQL 8.0 + `gorm.io/gorm`（搭配 MySQL 驱动）
  * **缓存与分布式组件**：Redis 7.0 + `github.com/redis/go-redis/v9`
  * **鉴权与配置**：`golang-jwt/jwt/v5`、`spf13/viper`、`uber-go/zap`（高性能日志）
  * **性能分析与压测**：Go 原生 `pprof` + `ghz`（专门用于 gRPC 压测的工具）

#### 2. Monorepo 工程目录规划
```bash
mcpe/                         # 根项目 (Monorepo)
├── CMakeLists.txt            # 顶层构建脚本
├── proto/                    # ⭐️ 跨语言核心契约 (单处修改，双端生成)
│   └── game_service.proto    # 定义 AuthService, PlayerDataService, RankService
├── include/mcpe/             # C++ 头文件
├── src/                      # C++ 网关与场景同步引擎源码
├── services/
│   └── endervault/           # ⭐️ EnderVault Go 微服务子项目 (独立 go.mod)
│       ├── go.mod
│       ├── go.sum
│       ├── main.go
│       ├── config/           # MySQL / Redis 连接配置 (yaml)
│       └── internal/
│           ├── auth/         # JWT 鉴权与 bcrypt 密码哈希
│           ├── storage/      # MySQL GORM 乐观锁 / Redis Cache-Aside
│           └── server/       # gRPC Server 实现桩
└── scripts/
    ├── generate_proto.sh     # 一键同时生成 C++ 和 Go 的桩代码
    └── dev_run.sh            # ⭐️ 一键协同启动与就绪探针脚本
```

#### 3. 伴随 C++ 网关自动启动机制（二选一或结合使用）
* **方案 A：脚本/编排级自动拉起（推荐工业级标准）**
  * 在 `scripts/dev_run.sh` 中一键拉起 Redis/MySQL，后台启动 `endervault`；
  * 执行端口健康探针（等待 `127.0.0.1:50051` 就绪），随后启动 C++ `mcpe`；
  * 注册退出钩子 `trap "kill -SIGTERM $GO_PID" EXIT`，实现单命令一键启停与优雅退出。
* **方案 B：C++ 进程内直接拉起子进程（单可执行文件体验）**
  * 在 `mcpe` 的 `main.cc` 启动时，检查 `./bin/endervault` 可执行文件；
  * 使用 `fork()` + `execl()` 派生 Go 服务子进程，利用 Linux `prctl(PR_SET_PDEATHSIG, SIGTERM)` 确保父进程异常崩溃时子进程自动退出，防止孤儿/僵尸进程；
  * `mcpe` 捕获 `SIGINT`/`SIGTERM` 时向子进程下发终止信号并通过 `waitpid` 回收资源。

---

### 二、 4 阶段保姆级任务拆解

```
   Phase 1           Phase 2           Phase 3           Phase 4
┌───────────┐     ┌───────────┐     ┌───────────┐     ┌───────────┐
│ Go速成与  │ ──►│ 数据库建模 │ ──►│ 事务一致性 │ ──►│ C++与Go联调│
│ gRPC契约定义│     │ 鉴权与缓存 │     │ 分布式锁/榜单│    │ 压测与pprof │
└───────────┘     └───────────┘     └───────────┘     └───────────┘
```

---

#### 阶段 1：Go 语言速成与 gRPC 跨语言通信基座（第 1~4 天）

* **目标**：掌握 Go 语言并发与核心语法，定义通信 `.proto` 文件，打通 C++（客户端桩）与 Go（服务端桩）之间的第一次 RPC 调用。

* **具体小任务**：
  1. **Go 基础速成（3 天）**：
     * 重点掌握：`struct` 结构体、`interface` 接口设计、`defer` 资源释放机制、`goroutine` 与 `channel` 协作、`context.Context` 超时与链路控制。
  2. **跨语言 Protobuf 协议设计**：
     * 编写 `game_service.proto`，统一声明服务接口与报文：
       * `AuthService`: `Login(LoginReq) returns (LoginResp)`、`VerifyToken(...)`
       * `PlayerDataService`: `GetPlayerData(...)`、`SavePlayerData(...)`
       * `RankService`: `SubmitScore(...)`、`GetTopRank(...)`
  3. **自动化代码生成**：
     * 配置 `protoc` 编译脚本，一次性生成 **Go 服务端代码** 和 **C++ 客户端代码**。
  4. **构建 Go gRPC 基础骨架**：
     * 初始化 gRPC Server，配置端口监听，编写简单的 Echo 逻辑；编写最小化 C++ gRPC 客户端发起请求，验证双端连通性。

* ⚠️ **常见错误与避坑点**：
  * **Protobuf 字段编号破坏**：Protobuf 依靠字段 Tag（如 `int32 uid = 1;` 中的 `1`）进行序列化。一旦定义好，绝不能随意更改已有字段的编号，否则会导致旧版本反序列化崩溃。
  * **gRPC 连接泄漏**：C++ 端绝不能在每次发起 RPC 时都重新创建 `grpc::Channel`。必须在初始化时维护**长连接 Channel 池**进行复用。

---

#### 阶段 2：数据库建模、ORM 封装与 Cache-Aside 登录鉴权（第 5~10 天）

* **目标**：完成 MySQL 表结构与索引设计，引入 Redis 实现经典的 Cache-Aside 缓存模式，完成玩家账号鉴权与防重复登录。

* **具体小任务**：
  1. **MySQL 数据表与索引设计**：
     * 设计 `users` 用户表：`id` (主键自增), `username` (唯一索引 `UNIQUE`), `password_hash`, `created_at`。
     * 设计 `player_profiles` 资产表：`user_id` (外键/索引), `level`, `exp`, `coins`, `inventory_json`, `version` (版本号，用于乐观锁), `updated_at`。
  2. **安全密码加密与 JWT 鉴权**：
     * 使用 `golang.org/x/crypto/bcrypt` 对密码进行加盐哈希存储（严禁存明文）。
     * 登录成功后生成带有效期的 JWT Token。
  3. **实现 Cache-Aside 缓存读写模式**：
     * 查询玩家数据时：先查 Redis；若命中则直接返回；若未命中（Cache Miss）则查询 MySQL，并回写 Redis（设置合理 TTL，如 30 分钟）。
  4. **防多端重复登录与踢人机制（Session 互斥）**：
     * 玩家登录时，向 Redis 写入 `session:uid -> gateway_id:token`。
     * 若发现该 UID 已存在有效 Session，且来自不同网关，触发 Kick 下线逻辑。

* ⚠️ **常见错误与避坑点**：
  * **缓存穿透与雪崩**：
    * 查询不存在的用户可能导致高频穿透打崩 MySQL，对空结果进行短期缓存（Cache-Null）或布隆过滤器防护。
    * Redis 缓存过期时间（TTL）必须增加随机抖动（如 `30m + rand(1~5m)`），防止大量 Key 同一秒失效导致缓存雪崩。
  * **GORM 零值更新陷阱**：
    * GORM 的 `Updates(struct)` 默认会忽略结构体中的零值（如 `0`、`false`、`""`）。更新金币为 0 时需使用 `Select` 或 `map[string]interface{}`。

---

#### 阶段 3：分布式并发控制、乐观锁与实时排行榜（第 11~15 天）

* **目标**：解决高并发下的数据一致性问题（防止道具/金币超卖与脏写），利用 Redis ZSet 搭建高性能全服排行榜。

* **具体小任务**：
  1. **MySQL 事务与乐观锁（防脏写）**：
     * 在保存玩家资产（`SavePlayerData`）时，SQL 使用版本号判定：
       `UPDATE player_profiles SET coins = ?, version = version + 1 WHERE user_id = ? AND version = ?`
     * 若受影响行数为 0，说明发生并发冲突，返回错误或触发重试，彻底杜绝数据覆盖。
  2. **基于 Redis 实现分布式互斥锁**：
     * 针对跨网关的敏感操作（如下线批量持久化、跨服交易），封装基于 Redis 的分布式锁：
       * 加锁：`SET lock:uid unique_request_id NX PX 3000`
       * 解锁：**必须使用 Lua 脚本**原子比对 `unique_request_id` 确认是自己持有的锁后再删除，防止因业务卡顿误删其他协程的锁。
  3. **基于 Redis Sorted Set (ZSet) 的全服实时排行榜**：
     * 玩家获得积分/挖掘方块时，调用 `ZADD leaderboard:score <score> <uid>`。
     * 提供分页查询接口：利用 `ZREVRANGE` 获取 Top 100 榜单，利用 `ZREVRANK` 秒级查询指定玩家的全服当前名次。

* ⚠️ **常见错误与避坑点**：
  * **分布式锁“死锁”与“误删”**：加锁必须带过期时间（Lease Time）；解锁必须用 Lua 保证原子性，绝不能先 `GET` 出来判断再调用 `DEL`（非原子操作必出并发 Bug）。
  * **BigKey 风险**：如果排行榜参与人数达到数百万，单个 ZSet 占用内存过大。但在本设计中维护 Top 10,000 以内即可，超出部分通过定期异步任务清理。

---

#### 阶段 4：双系统联调、压测与 pprof 性能诊断（第 16~20 天）

* **目标**：将 C++ 网关与 Go 中心服完整串联，进行全链路测试，使用压测工具分析 Go 服务在高 QPS 下的性能表现。

* **具体小任务**：
  1. **全链路端到端打通**：
     * 客户端连接 C++ 网关 $\to$ C++ 网关发起异步 gRPC 访问 Go 鉴权 $\to$ 验证通过进入世界。
     * 客户端在游戏中破坏方块/获得经验 $\to$ 退出游戏时 C++ 网关 RPC 通知 Go 中心服落库 MySQL 并刷新 Redis 榜单。
  2. **使用 `ghz` 进行 gRPC 专项压测**：
     * 编写测试脚本，使用 `ghz` 工具模拟 10,000+ 并发 RPC 请求轰炸 `Login` 和 `GetPlayerData` 接口。
     * 调整 MySQL 连接池（`SetMaxOpenConns`, `SetMaxIdleConns`）和 Redis 连接池大小，观察吞吐量变化。
  3. **利用 Go 原生 `pprof` 进行性能分析**：
     * 开启 `net/http/pprof`，在压测期间抓取 CPU 和 Heap 内存分配火焰图：
       `go tool pprof -http=:8080 http://localhost:6060/debug/pprof/profile`
     * 分析热点：定位是否存在多余的 JSON 反序列化开销或锁竞争。

* ⚠️ **常见错误与避坑点**：
  * **C++ 阻塞事件循环**：C++ 网关调用 Go 的 gRPC 时，**严禁在 World Tick 线程中执行同步阻塞等待**。必须在独立的 I/O Worker 线程发起 RPC，或者使用 gRPC 的 CompletionQueue 异步回调，拿到结果后再投递回逻辑主线程。
  * **数据库连接耗尽**：如果不配置连接池的最大闲置连接数，突发高并发请求会导致连接数瞬间飙升至 MySQL 上限抛出 `Too many connections`。

---

### 三、 必备学习资源与调试工具链

1. **Go 语言与微服务学习路径**：
   * **[A Tour of Go (官方在线教程)](https://tour.go-dev.org/welcome/1)**：交互式学习，2 天内刷完所有基础语法。
   * **[gRPC 官方 Go 快速指南](https://grpc.io/docs/languages/go/quickstart/)**：熟悉 `.proto` 编译与客户端/服务端桩代码编写。
   * **《Redis 设计与实现》（黄健宏 著）**：深入理解跳表（ZSet 底层原理）与分布式锁设计。
2. **调试与压测工具**：
   * **`ghz` (gRPC 压测神器)**：命令行工具，一行指令测试 gRPC 的 QPS、P95/P99 延迟分布。
   * **`RedisInsight` / `Navicat`**：可视化查看 Redis 键值、TTL 及 MySQL 索引执行计划（`EXPLAIN`）。
   * **Go 内置 `pprof`**：无需安装复杂第三方软件，一键在浏览器中查看 Go 协程状态、内存与 CPU 火焰图。

---

### 四、 简历项目经历描述包装（直接套用模板）

---

#### **项目名称：EnderVault - 基于 Go + gRPC 的分布式游戏数据中心与资产微服务**

* **项目定位**：基于 Go 语言与 gRPC 框架构建的高并发、低延迟分布式中心微服务系统，负责对接高性能游戏网关，承载玩家跨服分布式鉴权、数据持久化事务及全服实时排行榜等核心业务。
* **核心职责与技术实现**：
  * **微服务通信与契约设计**：基于 **Protobuf** 严格定义服务接口契约，实现 Go 中心服与 C++ 核心网关之间的**跨语言高性能 RPC 通信**；维护 gRPC 客户端长连接池与复用机制，大幅减少建连开销。
  * **缓存与持久化架构设计**：
    * 采用 **Cache-Aside 模式** 结合 Redis 与 MySQL，实现玩家数据的多级缓存读取与异步回写，使高频个人档案查询的 MySQL 命中率降低 90% 以上；
    * 合理设置带有随机抖动因子的 TTL，有效规避了缓存雪崩与缓存穿透隐患。
  * **高并发数据一致性与事务**：
    * 在玩家资产与道具修改链路中，采用 **MySQL 事务 + 版本号乐观锁（Optimistic Locking）** 机制，彻底避免高并发场景下的数据覆盖与脏写现象；
    * 基于 **Redis `SETNX` 与 Lua 脚本** 实现具备原子性校验的安全分布式锁，确保多网关并发环境下玩家会话（Session）唯一性与敏感操作互斥。
  * **实时榜单引擎**：利用 Redis **Sorted Set（ZSet，底层跳表实现）** 构建全服动态积分排行榜系统，支持十万级玩家分数的毫秒级动态刷新与 Top 100 范围快速检索。
  * **性能调优与容量压测**：
    * 借助 `ghz` 进行高并发 RPC 压力测试，对 MySQL/Redis 连接池核心参数（MaxOpen/MaxIdleConns）进行精细化调优；
    * 接入 Go 原生 **`pprof`** 诊断工具排查 GC 压力与内存分配热点，优化高频序列化路径，支撑单机 **XX,000+ QPS** 的稳定吞吐，P99 延迟控制在 XX ms 以内。