/**
 * @file test_portal.cc
 * @brief 传送门空间 AABB 碰撞检测与防抖冷却单元测试
 */

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include "lobby/PortalManager.h"

using namespace mcpe;
using namespace mcpe::lobby;

TEST(PortalManagerTest, CheckAndTriggerInsideAABB) {
  PortalManager manager;
  manager.addPortal(Portal{
      .name = "survival",
      .target_server = "survival",
      .display_name = "§a§l[生存服]",
      .bounding_box = AABB{
          .min_pt = Vector3d{10.0, 60.0, 10.0},
          .max_pt = Vector3d{15.0, 65.0, 15.0}
      }
  });

  const uint64_t sid = 1001;

  // 踩入包围盒内部
  const Vector3d inside_pos{12.5, 62.0, 12.5};
  const Portal* hit = manager.checkAndTrigger(sid, inside_pos);
  ASSERT_TRUE(hit != nullptr);
  EXPECT_EQ(hit->name, "survival");
  EXPECT_EQ(hit->target_server, "survival");
  EXPECT_EQ(hit->display_name, "§a§l[生存服]");
}

TEST(PortalManagerTest, CheckAndTriggerOutsideAABB) {
  PortalManager manager;
  manager.addPortal(Portal{
      .name = "survival",
      .target_server = "survival",
      .display_name = "§a§l[生存服]",
      .bounding_box = AABB{
          .min_pt = Vector3d{10.0, 60.0, 10.0},
          .max_pt = Vector3d{15.0, 65.0, 15.0}
      }
  });

  const uint64_t sid = 1001;

  // 在包围盒外部
  const Vector3d outside_pos{5.0, 60.0, 5.0};
  const Portal* hit = manager.checkAndTrigger(sid, outside_pos);
  EXPECT_TRUE(hit == nullptr);
}

TEST(PortalManagerTest, DebounceCooldown) {
  PortalManager manager;
  manager.addPortal(Portal{
      .name = "survival",
      .target_server = "survival",
      .display_name = "§a§l[生存服]",
      .bounding_box = AABB{
          .min_pt = Vector3d{0.0, 0.0, 0.0},
          .max_pt = Vector3d{10.0, 10.0, 10.0}
      }
  });

  const uint64_t sid = 1002;
  const Vector3d pos{5.0, 5.0, 5.0};

  // 1. 首次触发：通过
  const Portal* hit1 = manager.checkAndTrigger(sid, pos, std::chrono::seconds(2));
  EXPECT_TRUE(hit1 != nullptr);

  // 2. 紧接着再次触发：处于防抖冷却期，应被拦截
  const Portal* hit2 = manager.checkAndTrigger(sid, pos, std::chrono::seconds(2));
  EXPECT_TRUE(hit2 == nullptr);

  // 3. 换另一个玩家会话，应不受上一玩家冷却影响
  const uint64_t other_sid = 1003;
  const Portal* hit_other = manager.checkAndTrigger(other_sid, pos, std::chrono::seconds(2));
  EXPECT_TRUE(hit_other != nullptr);

  // 4. 显式指定 0ms 冷却后再次触发，应能立即通过
  const Portal* hit3 = manager.checkAndTrigger(sid, pos, std::chrono::milliseconds(0));
  EXPECT_TRUE(hit3 != nullptr);
}

TEST(PortalManagerTest, PlayerLeaveResetsCooldown) {
  PortalManager manager;
  manager.addPortal(Portal{
      .name = "survival",
      .target_server = "survival",
      .display_name = "§a§l[生存服]",
      .bounding_box = AABB{
          .min_pt = Vector3d{0.0, 0.0, 0.0},
          .max_pt = Vector3d{10.0, 10.0, 10.0}
      }
  });

  const uint64_t sid = 1004;
  const Vector3d pos{5.0, 5.0, 5.0};

  EXPECT_TRUE(manager.checkAndTrigger(sid, pos, std::chrono::seconds(10)) != nullptr);
  EXPECT_TRUE(manager.checkAndTrigger(sid, pos, std::chrono::seconds(10)) == nullptr);

  // 玩家离线注销
  manager.onPlayerLeave(sid);

  // 重新进入后立刻可以再次触发
  EXPECT_TRUE(manager.checkAndTrigger(sid, pos, std::chrono::seconds(10)) != nullptr);
}

TEST(PortalManagerTest, LoadFromIni) {
  const std::filesystem::path temp_ini = "test_portals_temp.ini";
  {
    std::ofstream out(temp_ini);
    out << "[Portal.survival]\n";
    out << "min = -65.0, 50.0, 5.0\n";
    out << "max = -60.0, 56.0, 11.0\n";
    out << "target = survival\n";
    out << "display_name = §a§l[生存服]\n\n";

    out << "[Portal.skyblock]\n";
    out << "min = -65.0, 50.0, -11.0\n";
    out << "max = -60.0, 56.0, -5.0\n";
    out << "target = skyblock\n";
    out << "display_name = §b§l[空岛服]\n";
  }

  PortalManager manager;
  ASSERT_TRUE(manager.loadFromIni(temp_ini));
  EXPECT_EQ(manager.portals().size(), 2u);

  // 测试命中 skyblock 传送门
  const Vector3d skyblock_pos{-62.0, 52.0, -8.0};
  const Portal* hit = manager.checkAndTrigger(2001, skyblock_pos);
  ASSERT_TRUE(hit != nullptr);
  EXPECT_EQ(hit->name, "skyblock");
  EXPECT_EQ(hit->target_server, "skyblock");

  std::error_code ec;
  std::filesystem::remove(temp_ini, ec);
}
