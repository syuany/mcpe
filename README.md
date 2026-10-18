# MCPE: Minecraft C++23 轻量集群大厅与挂机微服务引擎

MCPE 是一个基于 **C++23** 与 **Standalone Asio** 开发的高性能 Minecraft 1.12.2 服务端微服务引擎。专精于商业群组服的**轻量大厅/挂机中转 (Lobby/Limbo)** 与 **即插即用集群网关** 场景，常驻内存仅需约 **25MB**，冷启动耗时 **<20ms**。

> [!NOTE]
> **跑酷玩法归档说明**：跑酷玩法深度依赖 Minecraft 原版红石模拟、压力板电平、活塞推拉与复杂方块碰撞物理，与本项目“极致轻量微服务”的非目标冲突。跑酷模块已彻底解耦归档至 [`archive/parkour/`](archive/parkour/)，本项目全面收敛并聚焦于**商业大厅微服务、弹性分线中转与群组网关**。

---

## 核心功能

- **轻量大厅与漫游**：支持玩家进服认证、预烘焙只读区块极速加载、富文本聊天广播与同屏移动同步（基于九宫格 AOI 空间裁剪与写时复制零拷贝多播）。
- **多实例动态弹性分线**：基于 Actor 模型的 `LobbyManager`，支持 `lobby-1..N` 满员自动裂变与席位原子预留，各分线独占 20 TPS 物理线程，共享一份底层只读地形内存。
- **商业大厅管理与防作弊**：支持全服人数上限与分线配额配置（`/setmaxplayers`）、虚空跌落保护（Fall Reset）、大厅飞行切换（`/fly`）、出生点更新（`/setspawn`）及管理员动态口令鉴权（`/admin auth`）。
- **即插即用集群网关**：原生兼容 BungeeCord / Velocity IP Forwarding 协议穿透与正版 UUID/皮肤属性提取，支持 `0x18 Plugin Message (BungeeCord:Connect)` 传送门跨服路由。
- **极简容器交付**：多阶段构建生产级 Docker 镜像，体积 $\le 30\text{MB}$，毫秒级冷启动。

---

## 构建与运行

### 1. 环境要求
- C++23 兼容编译器（GCC 14+ / Clang 19+）
- CMake 3.14+ 与 Ninja（或 GNU Make）
- Python 3.10+（用于压测与验证脚本）

### 2. 本地原生编译与启动

```bash
# 1. 编译构建
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target server -j$(nproc)

# 2. 运行自动化单元测试 (229 项单元测试全绿通过)
ctest --test-dir build --output-on-failure

# 3. 启动服务端 (默认监听 25566 端口，首次启动自动生成 src/settings.ini)
./bin/server        # Linux
.\bin\server.exe    # Windows
```

### 3. Docker 容器化运行

```bash
# 单镜像构建与运行 (体积 ≤ 30MB)
docker build -t mcpe-lobby:latest .
docker run -d --name mcpe-lobby -p 25566:25566 mcpe-lobby:latest

# 或通过 docker-compose 一键拉起 (BungeeCord 25565 + MCPE 25566)
docker-compose up -d
```

### 4. 客户端连入与测试

- **原版客户端直连**：启动官方 Minecraft 1.12.2 客户端，多人游戏连接 `127.0.0.1:25566`。
- **连通性探测 (Ping/MOTD)**：`python scripts/test_ping.py`
- **千人并发压测 (模拟 1,000 Bots)**：
  ```bash
  python scripts/benchmark_swarm.py --host 127.0.0.1 --port 25566 --bots 1000 --rate 50 --mode limbo --duration 30
  ```

---

## 许可证

本项目基于 [MIT 许可证](LICENSE) 开源。
