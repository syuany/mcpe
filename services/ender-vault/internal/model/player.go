package model

import "time"

// Player 对应 MySQL 中的 players 表
type Player struct {
	ID         uint64    `gorm:"primaryKey;autoIncrement" json:"id"`
	UUID       string    `gorm:"size:36;uniqueIndex;not null" json:"uuid"`
	Username   string    `gorm:"size:32;index;not null" json:"username"`
	Coins      int64     `gorm:"not null;default:0" json:"coins"`
	Level      uint32    `gorm:"not null;default:1" json:"level"`
	Exp        uint64    `gorm:"not null;default:0" json:"exp"`
	LastServer string    `gorm:"size:32;default:'lobby'" json:"last_server"`
	CreatedAt  time.Time `json:"created_at"`
	UpdatedAt  time.Time `json:"updated_at"`
}

func (Player) TableName() string {
	return "players"
}

// PlayerVault 对应 player_vault 表 (跨服背包与末影箱)
type PlayerVault struct {
	PlayerUUID      string    `gorm:"primaryKey;size:36" json:"player_uuid"`
	InventoryData   string    `gorm:"type:longtext" json:"inventory_data"`
	EnderChestData  string    `gorm:"type:longtext" json:"ender_chest_data"`
	ExtraAttributes string    `gorm:"type:json" json:"extra_attributes"`
	Version         uint64    `gorm:"not null;default:0" json:"version"`
	UpdatedAt       time.Time `json:"updated_at"`
}

func (PlayerVault) TableName() string {
	return "player_vault"
}

// ParkourRecord 对应 parkour_records 表
type ParkourRecord struct {
	ID          uint64    `gorm:"primaryKey;autoIncrement" json:"id"`
	PlayerUUID  string    `gorm:"size:36;index;not null" json:"player_uuid"`
	PlayerName  string    `gorm:"size:32;not null" json:"player_name"`
	TrackID     string    `gorm:"size:64;index:idx_track_leaderboard,priority:1;not null" json:"track_id"`
	TimeMs      uint64    `gorm:"index:idx_track_leaderboard,priority:2;not null" json:"time_ms"`
	Checkpoints uint32    `gorm:"default:0" json:"checkpoints"`
	CreatedAt   time.Time `json:"created_at"`
}

func (ParkourRecord) TableName() string {
	return "parkour_records"
}

// PlayerFullData 聚合查询响应体
type PlayerFullData struct {
	UUID            string `json:"uuid"`
	Username        string `json:"username"`
	Coins           int64  `json:"coins"`
	Level           uint32 `json:"level"`
	Exp             uint64 `json:"exp"`
	LastServer      string `json:"last_server"`
	InventoryData   string `json:"inventory_data"`
	EnderChestData  string `json:"ender_chest_data"`
	ExtraAttributes string `json:"extra_attributes"`
	Version         uint64 `json:"version"`
}

// UpdatePlayerRequest 更新请求 DTO
type UpdatePlayerRequest struct {
	CoinsDelta      int64  `json:"coins_delta"`
	ExpDelta        int64  `json:"exp_delta"`
	LastServer      string `json:"last_server"`
	InventoryData   string `json:"inventory_data"`
	EnderChestData  string `json:"ender_chest_data"`
	ExtraAttributes string `json:"extra_attributes"`
	ExpectedVersion uint64 `json:"expected_version"`
}

// ParkourFinishRequest 冲线请求 DTO
type ParkourFinishRequest struct {
	PlayerUUID  string `json:"player_uuid" binding:"required"`
	PlayerName  string `json:"player_name" binding:"required"`
	TrackID     string `json:"track_id" binding:"required"`
	TimeMs      uint64 `json:"time_ms" binding:"required"`
	Checkpoints uint32 `json:"checkpoints"`
}

// LeaderboardEntry 排行榜项 DTO
type LeaderboardEntry struct {
	Rank       int    `json:"rank"`
	PlayerUUID string `json:"player_uuid"`
	PlayerName string `json:"player_name"`
	TimeMs     uint64 `json:"time_ms"`
}

// UpdateBatchTask 异步批量写入任务
type UpdateBatchTask struct {
	UUID       string
	CoinsDelta int64
	ExpDelta   int64
	LastServer string
}
