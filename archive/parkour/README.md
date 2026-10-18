# 跑酷玩法模块归档说明 (Parkour Module Archival)

## 归档原因 (Rationale for Archival)
1. **原版红石与复杂交互物理依赖**：
   在跑酷玩法的深入设计与实战验证中，发现跑酷玩法严重依赖 Minecraft 原版生存机制（如红石信号传导、测重压力板、活塞推拉、水流/粘液块跳跃、方块交互与碰撞物理）。
2. **违背轻量大厅核心定位**：
   本项目定位于极致轻量级（内存 $\le 30\text{MB}$、冷启动 $\le 50\text{ms}$）的高并发大厅/挂机微服务（Lobby/Limbo Engine）与即插即用集群网关。若为了跑酷而引入完整的原版红石模拟和方块复杂物理，将使工程陷入无边界的机制泥潭。
3. **架构决断**：
   经架构评审，决定彻底剥离并归档所有跑酷相关实现，将 MCPE 全面收敛聚焦于**纯粹的高性能商业大厅微服务、弹性分线调度、九宫格 AOI 与集群网关跨服路由**。

## 归档资产清单 (Archived Assets)
- `src/game/parkour/`: `ParkourInstance.h/.cc`, `ParkourMapConfig.h/.cc`
- `tests/`: `test_parkour_instance.cc`
- `scripts/`: `generate_spiral_parkour_schematic.py`, `test_sprint4_parkour.py`
- `assets/maps/parkour/`: `FLASH.zip`, `FLASH.mcpe_map`, `FLASH.ini`
