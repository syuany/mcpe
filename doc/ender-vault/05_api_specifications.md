# EnderVault 接口与契约规范 (API & Protocol Specification)

> **文档标识**：`MC-DOC-VAULT-05`  
> **服务名称**：EnderVault (分布式数据代理微服务)  
> **所属系统**：Minecraft Cluster (mc-cluster)  
> **版本**：v1.0.0  
> **最后更新**：2026-10-18  

---

## 一、 通用通信与错误码体系

### 1.1 统一 JSON 响应封装
所有 RESTful HTTP 接口返回数据均遵循统一的外层包裹格式：

```json
{
  "code": 0,
  "message": "success",
  "data": { ... }
}
```

### 1.2 错误码定义表
| 业务响应码 (code) | HTTP 状态码 | 含义说明 | 典型场景 |
| :--- | :--- | :--- | :--- |
| **0** | `200 OK` | 操作成功 | 请求正常处理 |
| **400** | `400 Bad Request` | 参数校验不合法 | 缺少必填字段（如缺少 `uuid` 或 `track_id`） |
| **404** | `404 Not Found` | 资源未找到 | 玩家未注册且未传递 `username` 进行初始化 |
| **409** | `409 Conflict` | 乐观锁 CAS 版本冲突 | 期望版本号与数据库当前版本不一致，防止覆盖 |
| **500** | `500 Internal Error` | 服务端内部异常 | MySQL 执行超时、Redis 挂起、语法错误 |
| **503** | `503 Unavailable` | 基础设施不健康 | 健康检查发现 MySQL 或 Redis 连接中断 |

---

## 二、 RESTful HTTP API 接口定义

服务默认监听端口：`8080`，所有业务路由挂载在 `/api/v1` 前缀下。

### 2.1 服务健康探针
* **URL**: `GET /health`
* **用途**: Docker Compose / Kubernetes 探针检测连通性。
* **响应示例 (`200 OK`)**:
```json
{
  "service": "ender-vault",
  "status": "ok",
  "database": "connected",
  "redis": "connected"
}
```

---

### 2.2 获取玩家档案与背包
* **URL**: `GET /api/v1/player/{uuid}/data`
* **Query 参数**:
  * `username` (string, 可选): 玩家昵称。若玩家首次入服，提供该字段将自动触发幂等创建。
* **响应示例 (`200 OK`)**:
```json
{
  "code": 0,
  "message": "success",
  "data": {
    "uuid": "00000000-0000-0000-0000-000000000001",
    "username": "Steve",
    "coins": 1000,
    "level": 1,
    "exp": 0,
    "last_server": "lobby",
    "inventory_data": "{\"slots\":[]}",
    "ender_chest_data": "{\"slots\":[]}",
    "extra_attributes": "{}",
    "version": 1
  }
}
```

---

### 2.3 增量/全量更新玩家数据
* **URL**: `POST /api/v1/player/{uuid}/data`
* **请求体 (JSON)**:
```json
{
  "coins_delta": 50,
  "exp_delta": 100,
  "last_server": "paper-parkour",
  "inventory_data": "{\"slots\":[{\"id\":276,\"count\":1}]}",
  "ender_chest_data": "{\"slots\":[]}",
  "extra_attributes": "{\"title\":\"ParkourMaster\"}",
  "expected_version": 1
}
```
* **说明**：
  - 若包含 `expected_version` 或 `inventory_data`，走同步事务 CAS 乐观锁逻辑。
  - 若仅包含 `coins_delta`, `exp_delta`, `last_server`，走后台 Channel 微批处理通道。

---

### 2.4 跑酷冲线战绩上报
* **URL**: `POST /api/v1/parkour/finish`
* **请求体 (JSON)**:
```json
{
  "player_uuid": "00000000-0000-0000-0000-000000000001",
  "player_name": "Steve",
  "track_id": "rooftop_sprint",
  "time_ms": 11250,
  "checkpoints": 5
}
```
* **响应示例 (`200 OK`)**:
```json
{
  "code": 0,
  "message": "recorded",
  "is_new_record": true,
  "rank": 2,
  "reward_coins": 100
}
```

---

### 2.5 获取赛道实时排行榜
* **URL**: `GET /api/v1/parkour/leaderboard?track_id=rooftop_sprint&limit=10`
* **响应示例 (`200 OK`)**:
```json
{
  "code": 0,
  "message": "success",
  "track_id": "rooftop_sprint",
  "entries": [
    {
      "rank": 1,
      "player_uuid": "00000000-0000-0000-0000-000000000002",
      "player_name": "Alex",
      "time_ms": 9840
    },
    {
      "rank": 2,
      "player_uuid": "00000000-0000-0000-0000-000000000001",
      "player_name": "Steve",
      "time_ms": 11250
    }
  ]
}
```

---

### 2.6 分布式锁获取与释放
* **获取锁 (`POST /api/v1/lock/acquire`)**:
```json
// Request
{ "resource_key": "player:trade:00000000-0000-0000-0000-000000000001", "ttl_seconds": 5 }
// Response
{ "acquired": true, "lock_token": "1729258900000_8374928" }
```
* **释放锁 (`POST /api/v1/lock/release`)**:
```json
// Request
{ "resource_key": "player:trade:00000000-0000-0000-0000-000000000001", "lock_token": "1729258900000_8374928" }
// Response
{ "success": true }
```

---

## 三、 gRPC Protobuf 契约映射 (`vault.proto`)

对于 C++ 游戏节点和 Java 子服，建议优先通过 gRPC 二进制协议直连（默认监听端口 `9090`）：

```protobuf
syntax = "proto3";

package mccluster.vault.v1;
option go_package = "mc-cluster/services/ender-vault/pkg/proto/v1;vaultv1";

service EnderVaultService {
    rpc GetPlayerData (GetPlayerDataRequest) returns (GetPlayerDataResponse);
    rpc UpdatePlayerData (UpdatePlayerDataRequest) returns (UpdatePlayerDataResponse);
    rpc RecordParkourFinish (ParkourFinishRequest) returns (ParkourFinishResponse);
    rpc GetLeaderboard (LeaderboardRequest) returns (LeaderboardResponse);
    rpc AcquireLock (AcquireLockRequest) returns (AcquireLockResponse);
    rpc ReleaseLock (ReleaseLockRequest) returns (ReleaseLockResponse);
}
```

跨语言调用拓扑：
1. **C++ (mcpe-lobby)**：利用 `protoc` 编译生成的 C++ Stub，通过 Asio 协程与 gRPC CompletionQueue 发起异步 RPC，零 JSON 反序列化开销。
2. **Java (Paper-Parkour)**：利用 `grpc-netty-shaded`，在 Bukkit 异步线程池中发起非阻塞 RPC 调用，规避主线程阻塞卡服。
