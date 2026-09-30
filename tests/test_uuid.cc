#include <gtest/gtest.h>

#include "protocol/AuthUtils.h"

using namespace mcpe::protocol;

TEST(AuthUtilsTest, ValidateUsername) {
  EXPECT_TRUE(AuthUtils::isValidUsername("Steve"));
  EXPECT_TRUE(AuthUtils::isValidUsername("Alex"));
  EXPECT_TRUE(AuthUtils::isValidUsername("Player_123"));
  EXPECT_TRUE(AuthUtils::isValidUsername("A"));
  EXPECT_TRUE(AuthUtils::isValidUsername("1234567890123456"));  // 16 字节合法

  EXPECT_FALSE(AuthUtils::isValidUsername(""));                   // 空名非法
  EXPECT_FALSE(AuthUtils::isValidUsername("12345678901234567"));  // >16 字节非法
  EXPECT_FALSE(AuthUtils::isValidUsername("Steve!"));             // 包含特殊字符
  EXPECT_FALSE(AuthUtils::isValidUsername("Alex "));              // 包含空格
  EXPECT_FALSE(AuthUtils::isValidUsername("中文玩家"));           // 包含非 ASCII
}

TEST(AuthUtilsTest, OfficialOfflineUuidVectors) {
  // 原版 Minecraft 官方离线计算基准对齐测试
  EXPECT_EQ(AuthUtils::generateOfflineUuid("Steve"), "5627dd98-e6be-3c21-b8a8-e92344183641");
  EXPECT_EQ(AuthUtils::generateOfflineUuid("Alex"), "36532b5e-c442-3dbb-a24c-c7e55d0f979a");
}