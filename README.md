# MCPE: Minecraft C++23 轻量大厅与跑酷微服务引擎

MCPE 是一个基于 **C++23** 与 **Standalone Asio** 开发的高性能 Minecraft 1.12.2 服务端微服务引擎。主要面向商业群组服的**轻量大厅/挂机中转 (Lobby/Limbo)** 与 **多实例跑酷小游戏** 场景，常驻内存仅需约 **25MB**，冷启动耗时 **<20ms**。

---

## 核心功能

- **轻量大厅与漫游**：支持玩家进服认证、区块流式加载、富文本聊天广播与同屏移动同步（基于九宫格 AOI 空间裁剪与写时复制零拷贝多播）。
- **多地图跑酷小游戏**：内置多张跑酷赛道地图，支持多人同场随进随跑、独立毫秒计时器、检查点跌落复位（Fall Reset）、排行榜以及单连接无缝跨世界切换。
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

# 2. 运行自动化单元测试 (232 项单元测试)
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
