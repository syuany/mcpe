# mc-cluster

一个由 C++ 大厅服、Go 数据服务、Paper 跑酷服和 BungeeCord 网关组成的 Minecraft 分布式集群。MySQL 和 Redis 提供数据持久化、缓存及排行榜功能，各服务通过 Docker Compose 统一运行。

## 构建与启动

环境要求：Docker 20.10+、Docker Compose v2。

在项目根目录执行：

```bash
docker compose up -d --build
```

查看运行状态：

```bash
docker compose ps
```

构建但不启动：

```bash
docker compose build
```
