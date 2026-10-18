# EnderVault 架构设计说明书 (Architecture Design Specification)

> **文档标识**：`MC-DOC-VAULT-03`  
> **服务名称**：EnderVault (分布式数据代理微服务)  
> **所属系统**：Minecraft Cluster (mc-cluster)  
> **版本**：v1.0.0  
> **最后更新**：2026-10-18  

---

## 一、 系统总体架构与拓扑 (System Topology)

EnderVault 在 `mc-cluster` 云原生微服务网络中充当 **统一数据代理与状态同步枢纽**。

```mermaid
flowchart TB
    subgraph Gateway["网关层"]
        Bungee["BungeeCord 代理网关 (TCP: 25565)"]
    end

    subgraph GameNodes["游戏逻辑服务层 (无状态/弱状态)"]
        Lobby["MCPE-Lobby 大厅服<br/>(C++20 / Asio / 内存 ≤ 30MB)"]
        Parkour["Paper 跑酷业务服<br/>(Paper 1.12.2 + ParkourPlugin)"]
        Survival["其他生存/空岛子服 (未来扩容)"]
    end

    subgraph DataProxy["核心数据代理层: EnderVault"]
        direction TB
        subgraph VaultPod["EnderVault (Go 微服务容器)"]
            Router["API 路由接入层<br/>(Gin HTTP / gRPC Server)"]
            Service["核心业务层<br/>(Singleflight / CAS / 批处理 / 分布式锁)"]
            BatchQ["内存 Channel 批处理缓冲区<br/>(Micro-batch Channel, 10000 缓冲)"]
            Repo["仓储抽象层 (Repository)"]
        end
    end

    subgraph StorageInfra["基础设施与持久化存储层"]
        Redis[("Redis 7.0 缓存与锁集群<br/>• Key-Value 缓存<br/>• ZSET 实时榜单<br/>• 分布式互斥锁")]
        MySQL[("MySQL 8.0 关系数据库<br/>• players (基础档案)<br/>• player_vault (跨服背包)<br/>• parkour_records (战绩归档)")]
    end

    Bungee -->|跨服路由| Lobby
    Bungee -->|跨服路由| Parkour
    Bungee -->|跨服路由| Survival

    Lobby -->|gRPC / HTTP REST| Router
    Parkour -->|HTTP REST 异步上报/拉取| Router
    Survival -.->|gRPC / HTTP| Router

    Router --> Service
    Service -->|高频数值写| BatchQ
    BatchQ -.->|定期 200ms / 定量 100 批量 Flush| Repo
    Service -->|强一致结构写| Repo
    Service -->|读请求| Repo

    Repo -->|SETNX / ZADD / GET / DEL / LUA| Redis
    Repo -->|事务写 / 索引回源| MySQL
```

---

## 二、 内部四层分层架构 (Clean Architecture)

EnderVault 遵循经典的分层架构与控制反转原则，各层之间通过 Go `interface` 解耦：

```mermaid
classDiagram
    class PlayerHandler {
        -repo Repository
        -svc PlayerService
        +HealthCheck(c *gin.Context)
        +GetPlayerData(c *gin.Context)
        +UpdatePlayerData(c *gin.Context)
        +RecordParkourFinish(c *gin.Context)
        +GetLeaderboard(c *gin.Context)
        +AcquireLock(c *gin.Context)
        +ReleaseLock(c *gin.Context)
    }

    class PlayerService {
        <<interface>>
        +GetPlayerData(ctx, uuid, username) (*PlayerFullData, error)
        +UpdatePlayerData(ctx, uuid, req) error
        +RecordParkour(ctx, req) (bool, uint64, int64, error)
        +GetLeaderboard(ctx, trackID, limit) ([]LeaderboardEntry, error)
        +AcquireLock(ctx, resourceKey, ttl) (string, bool, error)
        +ReleaseLock(ctx, resourceKey, token) (bool, error)
        +StartBatchWorker(ctx, batchSize, flushInterval)
        +StopBatchWorker()
    }

    class Repository {
        <<interface>>
        +GetPlayer(ctx, uuid) (*Player, error)
        +GetVault(ctx, uuid) (*PlayerVault, error)
        +CreatePlayerIfNotExist(ctx, uuid, username) (*Player, *PlayerVault, error)
        +UpdateVaultData(ctx, uuid, req) error
        +BatchFlushPlayers(ctx, tasks) error
        +GetPlayerCache(ctx, uuid) (*PlayerFullData, error)
        +SetPlayerCache(ctx, data, ttl) error
        +InvalidatePlayerCache(ctx, uuid) error
        +SaveParkourRecord(ctx, record) error
        +AddLeaderboardCache(ctx, trackID, uuid, name, timeMs) error
        +GetLeaderboard(ctx, trackID, limit) ([]LeaderboardEntry, error)
        +AcquireLock(ctx, key, token, ttl) (bool, error)
        +ReleaseLock(ctx, key, token) (bool, error)
        +PingDB(ctx) error
        +PingRedis(ctx) error
    }

    PlayerHandler --> PlayerService : 依赖接口
    PlayerService --> Repository : 依赖接口
```

1. **接入适配层 (`internal/handler`)**：
   - 负责 HTTP / gRPC 请求参数解析、入参校验（`binding:"required"`）、统一错误响应和 JSON 序列化。
2. **核心业务逻辑层 (`internal/service`)**：
   - 负责业务编排。维护 `singleflight.Group` 并发去重，协调 Redis 缓存与 MySQL 回源，管理 Channel 异步批量合并 Worker，组装分布式锁与跑酷发奖逻辑。
3. **数据仓储抽象层 (`internal/repository`)**：
   - 封装 GORM 事务、批量 SQL 构建、Redis 缓存 Key 拼装与 Lua 脚本执行。上层业务无须关心底层 SQL 拼装细节。
4. **领域模型与数据传输对象 (`internal/model`)**：
   - 定义数据库实体（`Player`, `PlayerVault`, `ParkourRecord`）与网络传输契约（DTO）。

---

## 三、 核心业务时序图 (Sequence Diagrams)

### 3.1 玩家登入与档案拉取流程 (Singleflight + Cache-Aside)

```mermaid
sequenceDiagram
    autonumber
    actor Player as 玩家客户端
    participant Game as 游戏节点 (mcpe-lobby / paper)
    participant Vault as EnderVault (Service)
    participant SF as singleflight.Group
    participant Redis as Redis 7.0
    participant MySQL as MySQL 8.0

    Player->>Game: 进入服务器
    Game->>Vault: GET /api/v1/player/{uuid}/data?username={name}
    Vault->>Redis: GET mc:player:{uuid}
    alt 缓存命中 (Cache Hit)
        Redis-->>Vault: 返回 PlayerFullData JSON
        Vault-->>Game: 200 OK (数据就绪, 耗时 < 2ms)
    else 缓存未命中 (Cache Miss - 高并发冷读)
        Vault->>SF: Do("get_player_" + uuid, Closure)
        note over SF: 相同 UUID 的并发请求在此排队<br/>仅放行单协程回源
        SF->>Redis: 二次检查缓存 (Double Check)
        alt 命中
            Redis-->>SF: 返回数据
        else 未命中
            SF->>MySQL: SELECT * FROM players WHERE uuid = ?
            alt 玩家不存在
                SF->>MySQL: 开启事务: INSERT players + INSERT player_vault
                MySQL-->>SF: 自动赋予初始资产 (1000 代币)
            else 玩家存在
                SF->>MySQL: SELECT * FROM player_vault WHERE player_uuid = ?
            end
            SF->>Redis: SETEX mc:player:{uuid} (BaseTTL 10m + Jitter 0~120s)
        end
        SF-->>Vault: 返回聚合后的 PlayerFullData
        Vault-->>Game: 200 OK (完成回填与同步)
    end
    Game-->>Player: 发送背包数据，允许行动
```

### 3.2 跨服切服/退服背包保存流程 (CAS 乐观锁防刷物)

```mermaid
sequenceDiagram
    autonumber
    participant Game as 游戏源节点 (离开前)
    participant Vault as EnderVault
    participant MySQL as MySQL 8.0
    participant Redis as Redis 7.0

    Game->>Vault: POST /api/v1/player/{uuid}/data<br/>{expected_version: 5, inventory_data: "..."}
    Vault->>MySQL: 开启事务
    Vault->>MySQL: UPDATE player_vault SET inventory_data = ?, version = version + 1<br/>WHERE player_uuid = ? AND version = 5
    alt 版本号匹配成功 (RowsAffected == 1)
        MySQL-->>Vault: 提交事务成功
        Vault->>Redis: DEL mc:player:{uuid} (淘汰旧缓存)
        Vault-->>Game: 200 OK (保存成功)
    else 版本号冲突 (RowsAffected == 0)
        MySQL-->>Vault: 回滚事务
        Vault-->>Game: 500 Error ("optimistic lock conflict: version mismatch")
        note over Game: 游戏服拒绝切服，触发异常告警，防止刷物
    end
```

### 3.3 跑酷冲线结算与实时排行榜刷新

```mermaid
sequenceDiagram
    autonumber
    actor Player as 玩家踩中终点压力板
    participant Plugin as Paper ParkourPlugin
    participant Vault as EnderVault
    participant MySQL as MySQL 8.0
    participant Redis as Redis 7.0

    Plugin->>Vault: POST /api/v1/parkour/finish<br/>{uuid, name, track_id: "rooftop_sprint", time_ms: 12450}
    Vault->>MySQL: INSERT INTO parkour_records (全量归档持久化)
    Vault->>Redis: ZSCORE mc:parkour:leaderboard:{track_id} {uuid}:{name}
    alt 当前成绩优于历史最高或首次冲线
        Vault->>Redis: ZADD mc:parkour:leaderboard:{track_id} 12450 {uuid}:{name}
    end
    Vault->>Redis: ZRANK mc:parkour:leaderboard:{track_id} {uuid}:{name}
    Redis-->>Vault: 得到当前最新全服名次
    Vault->>Vault: 异步投递发奖任务: CoinsDelta = +100, ExpDelta = +200
    Vault-->>Plugin: 200 OK {is_new_record: true, rank: 3, reward_coins: 100}
    Plugin-->>Player: 全服广播：玩家打破纪录！获得 100 代币！
```

---

## 四、 核心高并发与高可靠技术机制 (Core Mechanisms)

### 4.1 Singleflight 击穿防护与双重检查 (Double-Check)
当大量玩家在服务器重启后瞬间同时涌入，缓存全为空。如果没有任何防护，数千个协程会同时穿透至 MySQL 查询同一名热点玩家（或同屏所有玩家），造成数据库瞬时挂起。

EnderVault 的防护实现如下：
```go
v, err, _ := s.sfGroup.Do(fmt.Sprintf("get_player_%s", uuid), func() (interface{}, error) {
    // 1. 二次校验缓存 (Double Check)
    // 避免第 2~N 个等待排队的协程在获得锁后依然盲目查库
    if doubleCheck, _ := s.repo.GetPlayerCache(ctx, uuid); doubleCheck != nil {
        return doubleCheck, nil
    }
    // 2. 仅单协程回源 MySQL 并回填 Redis
    ...
})
```
* **效果**：将针对同一 Key 的瞬间 $N$ 个并发请求合并为 $1$ 次 MySQL 查询，其余协程共享该回源结果，数据库压力瞬间降低 $N$ 倍。

### 4.2 缓存雪崩与穿透防护
1. **防雪崩（Dynamic TTL Jitter）**：
   - 传统固定 TTL（如 10 分钟）会导致某一批次缓存同一时刻集体失效。
   - EnderVault 在回填缓存时增加 $0 \sim 120$ 秒的随机扰动：
     $$\text{TTL} = \text{BaseTTL} (10\text{m}) + \text{rand}(0, 120)\text{s}$$
   - 使缓存过期时间均匀平摊在时间轴上，消除毛刺峰值。
2. **防穿透（Cache Penetration）**：
   - 对非法 UUID 或不存在的玩家，系统应在 Redis 回填空对象标记（或极短 TTL 如 30s），防止黑客利用伪造 UUID 恶意轮询打爆数据库。

### 4.3 Channel 内存异步微批处理（Micro-batching）与写合并
游戏内高频的数值增减（如走动获得经验、小游戏金币跳动、跑酷通关）如果每次都执行一次完整的 MySQL 事务更新，数据库写 IOPS 将不堪重负。

EnderVault 采用了 **Producer-Consumer 内存微批处理架构**：

```mermaid
flowchart LR
    subgraph Producers["游戏服并发请求"]
        R1["玩家 A +10 金币"]
        R2["玩家 A +20 金币"]
        R3["玩家 B +50 金币"]
    end

    subgraph ChannelBuffer["Go 内存缓冲管道"]
        Ch["batchTaskCh (容量: 10000)<br/>非阻塞 select 投递"]
    end

    subgraph BatchWorker["后台单个消费 Worker 协程"]
        Timer["Ticker (200ms)"]
        Buffer["本地切片 Buffer (上限: 100 条)"]
        Merge["Map 合并同一 UUID 的数值增量:<br/>A: +30, B: +50"]
    end

    subgraph DB["MySQL 8.0"]
        TX["单一事务批量批量执行:<br/>UPDATE players SET coins = coins + ? WHERE uuid = ?"]
    end

    R1 --> Ch
    R2 --> Ch
    R3 --> Ch
    Ch --> Buffer
    Timer -.->|超时触发| Buffer
    Buffer --> Merge
    Merge --> TX
```

* **写合并（Write Coalescing）**：如果同一玩家在 200ms 内触发了多次金币变动，微批处理将它们在内存中累加为单次 SQL，极大减少行锁等待与 Redo Log 刷盘。
* **过载降级保护**：当缓冲管道满载时，通过 `select default` 优雅降级为同步写入，绝不盲目丢弃任务。

### 4.4 Redis + Lua 原子分布式互斥锁
分布式锁用于切服时资产独占保护。为了避免传统锁“超时后误删他人锁”的经典并发安全隐患，释放锁时必须采用 Lua 脚本进行校验：

```lua
-- KEYS[1]: lockKey (如 mc:lock:player:uuid)
-- ARGV[1]: token (持有者唯一标识)
if redis.call("get", KEYS[1]) == ARGV[1] then
    return redis.call("del", KEYS[1])
else
    return 0
end
```
* **原子性保证**：Redis 执行 Lua 脚本具备绝对单线程原子性，确保“校验 Token”与“执行 DEL”一气呵成，杜绝网络竞态。

---

## 五、 生命周期与优雅停机 (Graceful Shutdown)

```mermaid
sequenceDiagram
    autonumber
    participant OS as 操作系统 (Docker / K8s)
    participant Main as main.go (Signal Monitor)
    participant Worker as BatchWorker (Goroutine)
    participant DB as MySQL 8.0
    participant HTTP as HTTP Server

    OS->>Main: 发送 SIGTERM / SIGINT 信号
    Main->>HTTP: srv.Shutdown(ctx, 5s 超时)
    note over HTTP: 停止接收外部请求，等待存量请求处理完毕
    Main->>Worker: close(stopCh) 通知 Worker 退出
    Worker->>Worker: 收集剩余 Buffer 中所有待写任务
    Worker->>DB: 强制执行最终 Flush 事务落盘
    Worker-->>Main: wg.Wait() 阻塞等待 Worker 彻底安全退出
    Main->>Main: 关闭 Redis / MySQL 连接池
    Main-->>OS: 进程安全退出 (Exit 0, 零数据丢失)
```
