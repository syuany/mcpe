/**
 * @file test_parkour_instance.cc
 * @brief 跑酷小游戏实例（起点计时、多级检查点、虚空跌落拉回检查点、终点毫秒结算与 60 Ticks 自动回大厅）单元测试
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "game/InstanceManager.h"
#include "game/parkour/ParkourInstance.h"
#include "game/parkour/ParkourMapConfig.h"
#include "protocol/AuthUtils.h"
#include "world/World.h"

using namespace mcpe;
using namespace mcpe::game;
using namespace mcpe::game::parkour;
using namespace mcpe::protocol;
using namespace mcpe::world;

TEST(ParkourInstanceTest, FullParkourLifecycleStartCheckpointFallResetAndFinish) {
  auto pk_world = std::make_shared<World>();

  ParkourMapConfig cfg;
  cfg.map_name = "spiral";
  cfg.spawn_point = Vector3d{32.5, 33.0, 32.5};
  cfg.spawn_rotation = Rotation{0.0f, 0.0f};
  cfg.fall_reset_y = 20.0;
  cfg.start_box = AABB{Vector3d{30.5, 32.0, 34.0}, Vector3d{34.5, 36.0, 36.5}};

  ParkourCheckpoint cp1;
  cp1.box = AABB{Vector3d{34.0, 39.0, 21.0}, Vector3d{38.0, 43.0, 25.0}};
  cp1.respawn_pos = Vector3d{36.0, 41.0, 23.0};
  cp1.respawn_rot = Rotation{180.0f, 0.0f};
  cfg.checkpoints.push_back(cp1);

  cfg.finish_box = AABB{Vector3d{30.0, 50.0, 30.0}, Vector3d{35.0, 54.0, 35.0}};

  std::vector<std::pair<uint64_t, Vector3d>> teleports;
  uint64_t returned_to_lobby_sid = 0;

  ParkourInstance parkour(
      "parkour-spiral-1",
      pk_world,
      cfg,
      25,
      [](uint64_t /*sid*/, ByteBuffer /*pkt*/) {}
  );

  parkour.setTeleportCallback([&](uint64_t sid, const Vector3d& pos, const Rotation& /*rot*/) {
    teleports.emplace_back(sid, pos);
  });
  parkour.setReturnToLobbyCallback([&](uint64_t sid) {
    returned_to_lobby_sid = sid;
  });

  // 1. 玩家加入跑酷实例
  WorldEvent join_ev;
  join_ev.session_id = 1;
  join_ev.entity_id = 101;
  PlayerJoinEvent join_payload;
  join_payload.setName("SpeedRunner");
  join_payload.uuid = AuthUtils::generateOfflineUuidBytes("SpeedRunner");
  join_ev.payload = join_payload;
  parkour.pushEvent(std::move(join_ev));
  parkour.stepTick();

  const auto* st = parkour.getRunnerState(1);
  ASSERT_NE(st, nullptr);
  EXPECT_FALSE(st->running);
  EXPECT_EQ(st->current_checkpoint_idx, -1);

  // 2. 玩家踏入起点包围盒 (32.5, 33.0, 35.0) -> 计时自动启动！
  auto push_move = [&](const Vector3d& pos) {
    WorldEvent ev;
    ev.session_id = 1;
    ev.entity_id = 101;
    PlayerMoveEvent mv;
    mv.position = pos;
    mv.has_position = true;
    mv.on_ground = true;
    ev.payload = mv;
    parkour.pushEvent(std::move(ev));
    parkour.stepTick();
  };

  push_move(Vector3d{32.5, 33.0, 35.0});
  st = parkour.getRunnerState(1);
  ASSERT_NE(st, nullptr);
  EXPECT_TRUE(st->running);
  EXPECT_FALSE(st->finished);

  // 3. 玩家跳入第 1 号检查点包围盒 (36.0, 41.0, 23.0) -> 激活检查点 #0
  push_move(Vector3d{36.0, 41.0, 23.0});
  st = parkour.getRunnerState(1);
  EXPECT_EQ(st->current_checkpoint_idx, 0);
  EXPECT_DOUBLE_EQ(st->respawn_pos.x, 36.0);
  EXPECT_DOUBLE_EQ(st->respawn_pos.y, 41.0);
  EXPECT_DOUBLE_EQ(st->respawn_pos.z, 23.0);

  // 4. 玩家失足从远端跌入虚空 (100.0, 10.0, 100.0, Y < 20.0)：
  //    断言触发 Fall Reset 拉回第 1 号检查点 (36.0, 41.0, 23.0)，计时不中断，且 EntityManager 坐标同步！
  push_move(Vector3d{100.0, 10.0, 100.0});
  ASSERT_EQ(teleports.size(), 1u);
  EXPECT_EQ(teleports[0].first, 1u);
  EXPECT_DOUBLE_EQ(teleports[0].second.x, 36.0);
  EXPECT_DOUBLE_EQ(teleports[0].second.y, 41.0);
  EXPECT_DOUBLE_EQ(teleports[0].second.z, 23.0);

  st = parkour.getRunnerState(1);
  EXPECT_TRUE(st->running);
  EXPECT_EQ(st->fall_count, 1u);

  const auto* ent = parkour.entityManager().getPlayer(1);
  ASSERT_NE(ent, nullptr);
  EXPECT_DOUBLE_EQ(ent->position.x, 36.0);
  EXPECT_DOUBLE_EQ(ent->position.y, 41.0);
  EXPECT_DOUBLE_EQ(ent->position.z, 23.0);

  // 5. 玩家冲入终点包围盒 (32.5, 51.0, 32.5) -> 完成比赛并写入排行榜！
  push_move(Vector3d{32.5, 51.0, 32.5});
  st = parkour.getRunnerState(1);
  EXPECT_FALSE(st->running);
  EXPECT_TRUE(st->finished);
  EXPECT_GE(st->last_elapsed_ms, 1);
  ASSERT_EQ(parkour.leaderboard().size(), 1u);
  EXPECT_EQ(parkour.leaderboard()[0].username, "SpeedRunner");

  // 6. 推进 60 个物理 Ticks (模拟 3 秒完赛展示期) -> 自动触发返回大厅回调！
  for (uint32_t i = 0; i < ParkourInstance::kReturnLobbyDelayTicks; ++i) {
    parkour.stepTick();
  }
  EXPECT_EQ(returned_to_lobby_sid, 1u);
}

TEST(ParkourInstanceTest, CrossInstanceSeamlessTransferBetweenLobbyAndParkour) {
  auto lobby_world = std::make_shared<World>();
  auto spiral_world = std::make_shared<World>();

  InstanceManager mgr(lobby_world, 50, 5, 25, false);

  ParkourMapConfig spiral_cfg;
  spiral_cfg.map_name = "spiral";
  mgr.registerParkourMap(spiral_cfg, spiral_world);

  std::vector<std::string> switched_worlds;
  mgr.setWorldSwitchCallback([&](uint64_t /*sid*/, const std::shared_ptr<IGameInstance>& target) {
    switched_worlds.push_back(target->instanceId());
  });

  // 1. 玩家初始进入 lobby-1
  auto lobby = mgr.acquireLobbyForPlayer(42);
  ASSERT_NE(lobby, nullptr);
  ASSERT_TRUE(mgr.transferPlayer(
      42, 201, "Runner42", AuthUtils::generateOfflineUuidBytes("Runner42"), lobby
  ));
  lobby->stepTick();
  EXPECT_TRUE(lobby->entityManager().hasPlayer(42));

  // 2. 玩家执行 /join spiral 切入跑酷实例 parkour-spiral-1
  auto pk_inst = mgr.acquireParkourForPlayer(42, "spiral");
  ASSERT_NE(pk_inst, nullptr);
  EXPECT_EQ(pk_inst->instanceId(), "parkour-spiral-1");

  ASSERT_TRUE(mgr.transferPlayer(
      42, 201, "Runner42", AuthUtils::generateOfflineUuidBytes("Runner42"), pk_inst
  ));
  lobby->stepTick();
  pk_inst->stepTick();

  // 验证旧大厅已销毁该实体，新跑酷实例已生成该实体！
  EXPECT_FALSE(lobby->entityManager().hasPlayer(42));
  EXPECT_TRUE(pk_inst->entityManager().hasPlayer(42));
  ASSERT_EQ(switched_worlds.size(), 2u);
  EXPECT_EQ(switched_worlds[0], "lobby-1");
  EXPECT_EQ(switched_worlds[1], "parkour-spiral-1");
}

TEST(ParkourInstanceTest, DeveloperModeFlightAndConfigMutations) {
  auto pk_world = std::make_shared<World>();
  ParkourMapConfig cfg;
  cfg.map_name = "test_dev_map";
  cfg.spawn_point = Vector3d{10.0, 60.0, 10.0};
  cfg.fall_reset_y = 20.0;

  std::vector<Vector3d> teleports;
  ParkourInstance parkour(
      "parkour-dev-1",
      pk_world,
      cfg,
      10,
      [](uint64_t /*sid*/, ByteBuffer /*pkt*/) {}
  );
  parkour.setTeleportCallback([&](uint64_t /*sid*/, const Vector3d& pos, const Rotation& /*rot*/) {
    teleports.push_back(pos);
  });

  // 1. 玩家加入并开启开发者模式
  WorldEvent join_ev;
  join_ev.session_id = 101;
  join_ev.entity_id = 501;
  PlayerJoinEvent join_payload;
  join_payload.setName("DevBuilder");
  join_payload.uuid = AuthUtils::generateOfflineUuidBytes("DevBuilder");
  join_ev.payload = join_payload;
  parkour.pushEvent(std::move(join_ev));
  parkour.stepTick();
  EXPECT_FALSE(parkour.isDeveloperMode(101));

  parkour.setDeveloperMode(101, true);
  EXPECT_TRUE(parkour.isDeveloperMode(101));

  // 2. 开发者自由飞行至虚空 (Y = 5.0 < fall_reset_y = 20.0)，验证不会被跌落强行拉回
  WorldEvent move_ev;
  move_ev.session_id = 101;
  move_ev.entity_id = 501;
  PlayerMoveEvent mv;
  mv.position = Vector3d{10.0, 5.0, 10.0};
  mv.has_position = true;
  mv.on_ground = true;
  move_ev.payload = mv;
  parkour.pushEvent(std::move(move_ev));
  parkour.stepTick();
  EXPECT_TRUE(teleports.empty());

  // 3. 开发者在现场标定参数：设置出生点、起点、多级检查点与终点
  EXPECT_TRUE(parkour.setSpawnPoint(101, Vector3d{15.0, 65.0, 15.0}, Rotation{90.0f, 0.0f}));
  EXPECT_EQ(parkour.mapConfig().spawn_point.x, 15.0);

  EXPECT_TRUE(parkour.setStartBox(101, Vector3d{15.0, 65.0, 15.0}));
  EXPECT_TRUE(parkour.mapConfig().start_box.contains(Vector3d{15.0, 65.0, 15.0}));

  EXPECT_TRUE(parkour.setCheckpoint(101, Vector3d{25.0, 70.0, 25.0}, Rotation{0.0f, 0.0f}, 1));
  ASSERT_EQ(parkour.mapConfig().checkpoints.size(), 1u);
  EXPECT_EQ(parkour.mapConfig().checkpoints[0].respawn_pos.x, 25.0);

  EXPECT_TRUE(parkour.setFinishBox(101, Vector3d{50.0, 80.0, 50.0}));
  EXPECT_TRUE(parkour.mapConfig().finish_box.contains(Vector3d{50.0, 80.0, 50.0}));

  const std::string info = parkour.getCourseInfo();
  EXPECT_NE(info.find("test_dev_map"), std::string::npos);
  EXPECT_NE(info.find("检查点总数: 1 个"), std::string::npos);

  // 清理测试生成的伴生 INI 文件
  std::error_code ec;
  std::filesystem::remove(parkour.companionIniPath(), ec);
}

TEST(ParkourInstanceTest, SpeedZonesAndPotionEffectAcceleration) {
  auto pk_world = std::make_shared<World>();

  ParkourMapConfig cfg;
  cfg.map_name = "test_speed_course";
  cfg.spawn_point = Vector3d{10.0, 30.0, 10.0};
  cfg.fall_reset_y = 20.0;
  cfg.start_box = AABB{Vector3d{9.0, 29.0, 9.0}, Vector3d{11.0, 33.0, 11.0}};
  cfg.start_speed_amplifier = 0;  // 起跑即给 Speed I

  ParkourSpeedZone z1;
  z1.name = "zone_1";
  z1.box = AABB{Vector3d{20.0, 29.0, 20.0}, Vector3d{24.0, 33.0, 24.0}};
  z1.amplifier = 1;  // Speed II
  cfg.speed_zones.push_back(z1);

  ParkourSpeedZone z2;
  z2.name = "zone_2";
  z2.box = AABB{Vector3d{30.0, 29.0, 30.0}, Vector3d{34.0, 33.0, 34.0}};
  z2.amplifier = 8;  // Speed IX
  cfg.speed_zones.push_back(z2);

  ParkourCheckpoint cp1;
  cp1.box = AABB{Vector3d{40.0, 29.0, 40.0}, Vector3d{44.0, 33.0, 44.0}};
  cp1.respawn_pos = Vector3d{42.0, 30.0, 42.0};
  cp1.speed_amplifier = 2;  // Speed III
  cfg.checkpoints.push_back(cp1);

  cfg.finish_box = AABB{Vector3d{50.0, 29.0, 50.0}, Vector3d{54.0, 33.0, 54.0}};

  std::vector<ByteBuffer> sent_packets;
  ParkourInstance parkour(
      "parkour-speed-test",
      pk_world,
      cfg,
      10,
      [&](uint64_t /*sid*/, ByteBuffer pkt) {
        sent_packets.push_back(std::move(pkt));
      }
  );

  // 1. 玩家加入
  WorldEvent join_ev;
  join_ev.session_id = 42;
  join_ev.entity_id = 999;
  PlayerJoinEvent join_payload;
  join_payload.setName("FlashRunner");
  join_payload.uuid = AuthUtils::generateOfflineUuidBytes("FlashRunner");
  join_ev.payload = join_payload;
  parkour.pushEvent(std::move(join_ev));
  parkour.stepTick();

  const auto* st = parkour.getRunnerState(42);
  ASSERT_NE(st, nullptr);
  EXPECT_EQ(st->current_speed_amplifier, -1);

  auto push_move = [&](const Vector3d& pos) {
    WorldEvent ev;
    ev.session_id = 42;
    ev.entity_id = 999;
    PlayerMoveEvent mv;
    mv.position = pos;
    mv.has_position = true;
    mv.on_ground = true;
    ev.payload = mv;
    parkour.pushEvent(std::move(ev));
    parkour.stepTick();
  };

  // 2. 踩入起点 -> 触发起跑初始速度 Speed I (amplifier = 0)
  push_move(Vector3d{10.0, 30.0, 10.0});
  EXPECT_TRUE(st->running);
  EXPECT_EQ(st->current_speed_amplifier, 0);

  // 3. 踩入变速区 1 -> 自动切换到 Speed II (amplifier = 1)
  push_move(Vector3d{22.0, 30.0, 22.0});
  EXPECT_EQ(st->current_speed_amplifier, 1);
  EXPECT_EQ(st->current_speed_zone_idx, 0);

  // 4. 踩入变速区 2 -> 自动切换到 Speed IX (amplifier = 8)
  push_move(Vector3d{32.0, 30.0, 32.0});
  EXPECT_EQ(st->current_speed_amplifier, 8);
  EXPECT_EQ(st->current_speed_zone_idx, 1);

  // 5. 踩入第 1 号检查点 -> 设定检查点指定速度 Speed III (amplifier = 2)
  push_move(Vector3d{42.0, 30.0, 42.0});
  EXPECT_EQ(st->current_checkpoint_idx, 0);
  EXPECT_EQ(st->current_speed_amplifier, 2);

  // 6. 掉落虚空 (Y = 10.0 < fall_reset_y = 20.0) -> 复位并恢复该检查点速度 (amplifier = 2)
  push_move(Vector3d{45.0, 10.0, 45.0});
  EXPECT_EQ(st->fall_count, 1u);
  EXPECT_EQ(st->current_speed_amplifier, 2);

  // 7. 踩入终点 -> 完赛并清除速度状态
  push_move(Vector3d{52.0, 30.0, 52.0});
  EXPECT_TRUE(st->finished);
  EXPECT_EQ(st->current_speed_amplifier, -1);

  // 8. 验证 INI 存储与加载完整保留 speed_zones
  std::error_code ec;
  const auto temp_ini = std::filesystem::temp_directory_path() / "test_speed_cfg.ini";
  EXPECT_TRUE(cfg.saveToIni(temp_ini));

  auto loaded_cfg = ParkourMapConfig::loadFromIni("test_speed_course", temp_ini);
  ASSERT_TRUE(loaded_cfg.has_value());
  EXPECT_EQ(loaded_cfg->start_speed_amplifier, 0);
  ASSERT_EQ(loaded_cfg->speed_zones.size(), 2u);
  EXPECT_EQ(loaded_cfg->speed_zones[0].amplifier, 1);
  EXPECT_EQ(loaded_cfg->speed_zones[1].amplifier, 8);
  ASSERT_EQ(loaded_cfg->checkpoints.size(), 1u);
  EXPECT_EQ(loaded_cfg->checkpoints[0].speed_amplifier, 2);

  std::filesystem::remove(temp_ini, ec);
}
