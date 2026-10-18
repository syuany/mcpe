/**
 * @file test_ini_config.cc
 * @brief IniConfig 双层配置文件（settings.ini 与 <map>.ini）及 AABB 包围盒单元测试
 */

#include <gtest/gtest.h>

#include <filesystem>

#include "common/IniConfig.h"
#include "common/SpatialTypes.h"
#include "protocol/GameTypes.h"
#include "world/World.h"

using namespace mcpe;
using namespace mcpe::common;
using namespace mcpe::protocol;
using namespace mcpe::world;

TEST(IniConfigTest, ParseAndSerializeRoundTrip) {
  const std::filesystem::path tmp_ini = "/tmp/test_mcpe_settings.ini";

  IniConfig cfg;
  cfg.setInt("Server", "port", 25566);
  cfg.setInt("Server", "max_players", 500);
  cfg.setString("Server", "motd", "§aMCPE Test Server");
  cfg.setInt("Lobby", "max_players_per_instance", 50);
  cfg.setBool("Lobby", "allow_flight", true);
  cfg.setVector3d("World", "spawn_pos", Vector3d{32.5, 64.0, -16.25});

  ASSERT_TRUE(cfg.saveToFile(tmp_ini));

  IniConfig loaded;
  ASSERT_TRUE(loaded.loadFromFile(tmp_ini));
  EXPECT_TRUE(loaded.hasSection("Server"));
  EXPECT_TRUE(loaded.hasKey("Server", "port"));
  EXPECT_EQ(loaded.getInt("Server", "port", 0), 25566);
  EXPECT_EQ(loaded.getInt("Server", "max_players", 0), 500);
  EXPECT_EQ(loaded.getString("Server", "motd"), "§aMCPE Test Server");
  EXPECT_EQ(loaded.getInt("Lobby", "max_players_per_instance", 0), 50);
  EXPECT_TRUE(loaded.getBool("Lobby", "allow_flight", false));

  auto vec_opt = loaded.getVector3d("World", "spawn_pos");
  ASSERT_TRUE(vec_opt.has_value());
  EXPECT_NEAR(vec_opt->x, 32.5, 0.01);
  EXPECT_NEAR(vec_opt->y, 64.0, 0.01);
  EXPECT_NEAR(vec_opt->z, -16.25, 0.01);

  std::filesystem::remove(tmp_ini);
}

TEST(IniConfigTest, AABBBoundingBoxAndCommentsParsing) {
  constexpr std::string_view kRawIni = R"(
    ; Comment line with semicolon
    # Comment line with hash
    [Zone]
    start_min = 30.5, 32.0, 34.0
    start_max = 34.5, 36.0, 36.5
  )";

  IniConfig ini;
  ini.parseFromString(kRawIni);

  auto box_opt = ini.getAABB("Zone", "start_min", "start_max");
  ASSERT_TRUE(box_opt.has_value());
  EXPECT_TRUE(box_opt->contains(Vector3d{32.5, 33.0, 35.0}));
  EXPECT_FALSE(box_opt->contains(Vector3d{10.0, 33.0, 35.0}));
  EXPECT_FALSE(box_opt->contains(Vector3d{32.5, 20.0, 35.0}));
}

TEST(IniConfigTest, WorldIniSpawnConfigPreservesOtherSections) {
  const std::filesystem::path tmp_map_ini = "/tmp/test_lobby_extra.ini";

  IniConfig initial;
  initial.setString("Zone", "start_min", "0.0, 64.0, 0.0");
  initial.setString("Zone", "start_max", "2.0, 66.0, 2.0");
  ASSERT_TRUE(initial.saveToFile(tmp_map_ini));

  World w1;
  w1.setSpawnPoint(Vector3d{12.5, 45.0, -8.5});
  w1.setSpawnRotation(Rotation{90.0f, -5.0f});
  w1.setFallResetY(18.0);
  ASSERT_TRUE(w1.saveSpawnConfig(tmp_map_ini));

  World w2;
  ASSERT_TRUE(w2.loadSpawnConfig(tmp_map_ini));
  EXPECT_NEAR(w2.spawnPoint().x, 12.5, 0.01);
  EXPECT_NEAR(w2.spawnPoint().y, 45.0, 0.01);
  EXPECT_NEAR(w2.spawnPoint().z, -8.5, 0.01);
  EXPECT_NEAR(w2.spawnRotation().yaw, 90.0f, 0.01f);
  EXPECT_NEAR(w2.spawnRotation().pitch, -5.0f, 0.01f);
  EXPECT_NEAR(w2.fallResetY(), 18.0, 0.01);

  // 验证 [Zone] Section 依然被完好保留
  IniConfig reloaded;
  ASSERT_TRUE(reloaded.loadFromFile(tmp_map_ini));
  EXPECT_TRUE(reloaded.hasSection("Zone"));
  EXPECT_EQ(reloaded.getString("Zone", "start_min"), "0.0, 64.0, 0.0");

  std::filesystem::remove(tmp_map_ini);
}
