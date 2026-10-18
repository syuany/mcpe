-- ==============================================================================
-- Minecraft Cluster (mc-cluster) API 契约层数据库定义 (Schema)
-- ==============================================================================

CREATE DATABASE IF NOT EXISTS `mc_cluster` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE `mc_cluster`;

-- 1. 玩家全局基础数据表 (跨服共享)
CREATE TABLE IF NOT EXISTS `players` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '自增主键',
    `uuid` VARCHAR(36) NOT NULL COMMENT '玩家 Mojang / 离线 UUID',
    `username` VARCHAR(32) NOT NULL COMMENT '最新玩家游戏名',
    `coins` BIGINT NOT NULL DEFAULT 0 COMMENT '代币/游戏币余额',
    `level` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT '玩家全局等级',
    `exp` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '玩家当前经验值',
    `last_server` VARCHAR(32) DEFAULT 'lobby' COMMENT '最后所在服务器节点',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '创建时间',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`id`),
    UNIQUE KEY `uk_uuid` (`uuid`),
    KEY `idx_username` (`username`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='玩家全局核心数据表';

-- 2. 玩家跨服元数据与末影箱数据表 (EnderVault 代理管理)
CREATE TABLE IF NOT EXISTS `player_vault` (
    `player_uuid` VARCHAR(36) NOT NULL COMMENT '玩家 UUID',
    `inventory_data` LONGTEXT COMMENT '通用背包/道具数据序列化(JSON/Base64)',
    `ender_chest_data` LONGTEXT COMMENT '全局跨服末影箱数据',
    `extra_attributes` JSON COMMENT '扩展键值元数据(如称号、权限、特效开关等)',
    `version` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '乐观锁版本号',
    `updated_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '更新时间',
    PRIMARY KEY (`player_uuid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='玩家跨服存储与背包数据表';

-- 3. 跑酷关卡冲线成绩与排行榜记录表
CREATE TABLE IF NOT EXISTS `parkour_records` (
    `id` BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '记录自增 ID',
    `player_uuid` VARCHAR(36) NOT NULL COMMENT '玩家 UUID',
    `player_name` VARCHAR(32) NOT NULL COMMENT '冲线时玩家名',
    `track_id` VARCHAR(64) NOT NULL DEFAULT 'rooftop_sprint' COMMENT '跑酷赛道标识符',
    `time_ms` BIGINT UNSIGNED NOT NULL COMMENT '冲线通关耗时(毫秒)',
    `checkpoints` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '通过的检查点数量',
    `created_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '完成时间',
    PRIMARY KEY (`id`),
    KEY `idx_track_leaderboard` (`track_id`, `time_ms`),
    KEY `idx_player_history` (`player_uuid`, `created_at`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='跑酷赛道通关战绩榜单表';
