package repository

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"strconv"
	"strings"
	"time"

	"mc-cluster/services/ender-vault/internal/model"

	"github.com/redis/go-redis/v9"
	"gorm.io/gorm"
	"gorm.io/gorm/clause"
)

// Repository 定义存储层操作接口
type Repository interface {
	// 基础玩家数据
	GetPlayer(ctx context.Context, uuid string) (*model.Player, error)
	GetVault(ctx context.Context, uuid string) (*model.PlayerVault, error)
	CreatePlayerIfNotExist(ctx context.Context, uuid, username string) (*model.Player, *model.PlayerVault, error)
	UpdateVaultData(ctx context.Context, uuid string, req *model.UpdatePlayerRequest) error
	BatchFlushPlayers(ctx context.Context, tasks []*model.UpdateBatchTask) error

	// 缓存操作
	GetPlayerCache(ctx context.Context, uuid string) (*model.PlayerFullData, error)
	SetPlayerCache(ctx context.Context, data *model.PlayerFullData, ttl time.Duration) error
	InvalidatePlayerCache(ctx context.Context, uuid string) error

	// 跑酷榜单
	SaveParkourRecord(ctx context.Context, record *model.ParkourRecord) error
	AddLeaderboardCache(ctx context.Context, trackID, uuid, name string, timeMs uint64) error
	GetLeaderboard(ctx context.Context, trackID string, limit int) ([]model.LeaderboardEntry, error)

	// 分布式互斥锁 (Redis + Lua)
	AcquireLock(ctx context.Context, key, token string, ttl time.Duration) (bool, error)
	ReleaseLock(ctx context.Context, key, token string) (bool, error)

	// 健康检查
	PingDB(ctx context.Context) error
	PingRedis(ctx context.Context) error
}

type repositoryImpl struct {
	db  *gorm.DB
	rdb *redis.Client
}

func NewRepository(db *gorm.DB, rdb *redis.Client) Repository {
	return &repositoryImpl{
		db:  db,
		rdb: rdb,
	}
}

func (r *repositoryImpl) PingDB(ctx context.Context) error {
	sqlDB, err := r.db.DB()
	if err != nil {
		return err
	}
	return sqlDB.PingContext(ctx)
}

func (r *repositoryImpl) PingRedis(ctx context.Context) error {
	return r.rdb.Ping(ctx).Err()
}

func (r *repositoryImpl) GetPlayer(ctx context.Context, uuid string) (*model.Player, error) {
	var player model.Player
	err := r.db.WithContext(ctx).Where("uuid = ?", uuid).First(&player).Error
	if err != nil {
		return nil, err
	}
	return &player, nil
}

func (r *repositoryImpl) GetVault(ctx context.Context, uuid string) (*model.PlayerVault, error) {
	var vault model.PlayerVault
	err := r.db.WithContext(ctx).Where("player_uuid = ?", uuid).First(&vault).Error
	if err != nil {
		return nil, err
	}
	return &vault, nil
}

func (r *repositoryImpl) CreatePlayerIfNotExist(ctx context.Context, uuid, username string) (*model.Player, *model.PlayerVault, error) {
	player := &model.Player{
		UUID:       uuid,
		Username:   username,
		Coins:      1000, // 初始进服奖励
		Level:      1,
		Exp:        0,
		LastServer: "lobby",
	}

	vault := &model.PlayerVault{
		PlayerUUID:      uuid,
		InventoryData:   `{"slots":[]}`,
		EnderChestData:  `{"slots":[]}`,
		ExtraAttributes: `{}`,
		Version:         1,
	}

	err := r.db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		if err := tx.Clauses(clause.OnConflict{DoNothing: true}).Create(player).Error; err != nil {
			return err
		}
		if err := tx.Clauses(clause.OnConflict{DoNothing: true}).Create(vault).Error; err != nil {
			return err
		}
		return nil
	})

	if err != nil {
		return nil, nil, err
	}
	return player, vault, nil
}

func (r *repositoryImpl) UpdateVaultData(ctx context.Context, uuid string, req *model.UpdatePlayerRequest) error {
	return r.db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		// 1. 更新玩家基础数值 (支持原子增量)
		if req.CoinsDelta != 0 || req.ExpDelta != 0 || req.LastServer != "" {
			updates := map[string]interface{}{}
			if req.CoinsDelta != 0 {
				updates["coins"] = gorm.Expr("coins + ?", req.CoinsDelta)
			}
			if req.ExpDelta != 0 {
				updates["exp"] = gorm.Expr("exp + ?", req.ExpDelta)
			}
			if req.LastServer != "" {
				updates["last_server"] = req.LastServer
			}
			if err := tx.Model(&model.Player{}).Where("uuid = ?", uuid).Updates(updates).Error; err != nil {
				return err
			}
		}

		// 2. 更新背包与末影箱 (CAS 乐观锁校验)
		if req.InventoryData != "" || req.EnderChestData != "" || req.ExtraAttributes != "" {
			vUpdates := map[string]interface{}{
				"version": gorm.Expr("version + 1"),
			}
			if req.InventoryData != "" {
				vUpdates["inventory_data"] = req.InventoryData
			}
			if req.EnderChestData != "" {
				vUpdates["ender_chest_data"] = req.EnderChestData
			}
			if req.ExtraAttributes != "" {
				vUpdates["extra_attributes"] = req.ExtraAttributes
			}

			q := tx.Model(&model.PlayerVault{}).Where("player_uuid = ?", uuid)
			if req.ExpectedVersion > 0 {
				q = q.Where("version = ?", req.ExpectedVersion)
			}
			res := q.Updates(vUpdates)
			if res.Error != nil {
				return res.Error
			}
			if req.ExpectedVersion > 0 && res.RowsAffected == 0 {
				return errors.New("optimistic lock conflict: version mismatch")
			}
		}
		return nil
	})
}

func (r *repositoryImpl) BatchFlushPlayers(ctx context.Context, tasks []*model.UpdateBatchTask) error {
	if len(tasks) == 0 {
		return nil
	}

	// 合并同一玩家的增量
	merged := make(map[string]*model.UpdateBatchTask)
	for _, t := range tasks {
		if exist, ok := merged[t.UUID]; ok {
			exist.CoinsDelta += t.CoinsDelta
			exist.ExpDelta += t.ExpDelta
			if t.LastServer != "" {
				exist.LastServer = t.LastServer
			}
		} else {
			merged[t.UUID] = &model.UpdateBatchTask{
				UUID:       t.UUID,
				CoinsDelta: t.CoinsDelta,
				ExpDelta:   t.ExpDelta,
				LastServer: t.LastServer,
			}
		}
	}

	return r.db.WithContext(ctx).Transaction(func(tx *gorm.DB) error {
		for _, task := range merged {
			updates := map[string]interface{}{}
			if task.CoinsDelta != 0 {
				updates["coins"] = gorm.Expr("coins + ?", task.CoinsDelta)
			}
			if task.ExpDelta != 0 {
				updates["exp"] = gorm.Expr("exp + ?", task.ExpDelta)
			}
			if task.LastServer != "" {
				updates["last_server"] = task.LastServer
			}
			if len(updates) > 0 {
				if err := tx.Model(&model.Player{}).Where("uuid = ?", task.UUID).Updates(updates).Error; err != nil {
					return err
				}
			}
		}
		return nil
	})
}

// ------------------------------------------------------------------------------
// Redis 缓存操作
// ------------------------------------------------------------------------------

func (r *repositoryImpl) GetPlayerCache(ctx context.Context, uuid string) (*model.PlayerFullData, error) {
	key := fmt.Sprintf("mc:player:%s", uuid)
	val, err := r.rdb.Get(ctx, key).Result()
	if errors.Is(err, redis.Nil) {
		return nil, nil // 缓存未命中
	} else if err != nil {
		return nil, err
	}

	var data model.PlayerFullData
	if err := json.Unmarshal([]byte(val), &data); err != nil {
		return nil, err
	}
	return &data, nil
}

func (r *repositoryImpl) SetPlayerCache(ctx context.Context, data *model.PlayerFullData, ttl time.Duration) error {
	key := fmt.Sprintf("mc:player:%s", data.UUID)
	bytes, err := json.Marshal(data)
	if err != nil {
		return err
	}
	return r.rdb.Set(ctx, key, bytes, ttl).Err()
}

func (r *repositoryImpl) InvalidatePlayerCache(ctx context.Context, uuid string) error {
	key := fmt.Sprintf("mc:player:%s", uuid)
	return r.rdb.Del(ctx, key).Err()
}

// ------------------------------------------------------------------------------
// 跑酷成绩与排行榜 (MySQL 归档 + Redis ZSET 高并发实时榜)
// ------------------------------------------------------------------------------

func (r *repositoryImpl) SaveParkourRecord(ctx context.Context, record *model.ParkourRecord) error {
	return r.db.WithContext(ctx).Create(record).Error
}

func (r *repositoryImpl) AddLeaderboardCache(ctx context.Context, trackID, uuid, name string, timeMs uint64) error {
	key := fmt.Sprintf("mc:parkour:leaderboard:%s", trackID)
	member := fmt.Sprintf("%s:%s", uuid, name)

	// Redis ZSET 越小成绩越好 (使用 ZAddArgs 只在分数更优时更新)
	currentScore, err := r.rdb.ZScore(ctx, key, member).Result()
	if err == nil && float64(timeMs) >= currentScore {
		// 历史成绩更好，不覆盖
		return nil
	}

	return r.rdb.ZAdd(ctx, key, redis.Z{
		Score:  float64(timeMs),
		Member: member,
	}).Err()
}

func (r *repositoryImpl) GetLeaderboard(ctx context.Context, trackID string, limit int) ([]model.LeaderboardEntry, error) {
	key := fmt.Sprintf("mc:parkour:leaderboard:%s", trackID)
	// ZRangeWithScores 升序 (耗时越短排越前)
	zEntries, err := r.rdb.ZRangeWithScores(ctx, key, 0, int64(limit-1)).Result()
	if err != nil {
		return nil, err
	}

	var results []model.LeaderboardEntry
	for i, z := range zEntries {
		parts := strings.Split(fmt.Sprintf("%v", z.Member), ":")
		pUUID := parts[0]
		pName := pUUID
		if len(parts) > 1 {
			pName = parts[1]
		}
		results = append(results, model.LeaderboardEntry{
			Rank:       i + 1,
			PlayerUUID: pUUID,
			PlayerName: pName,
			TimeMs:     uint64(z.Score),
		})
	}
	return results, nil
}

// ------------------------------------------------------------------------------
// 基于 Redis 的分布式锁机制
// ------------------------------------------------------------------------------

func (r *repositoryImpl) AcquireLock(ctx context.Context, key, token string, ttl time.Duration) (bool, error) {
	lockKey := fmt.Sprintf("mc:lock:%s", key)
	return r.rdb.SetNX(ctx, lockKey, token, ttl).Result()
}

// Lua 脚本保证解锁原子性 (避免删掉其他协程持有的锁)
const releaseLockLua = `
if redis.call("get", KEYS[1]) == ARGV[1] then
    return redis.call("del", KEYS[1])
else
    return 0
end
`

func (r *repositoryImpl) ReleaseLock(ctx context.Context, key, token string) (bool, error) {
	lockKey := fmt.Sprintf("mc:lock:%s", key)
	res, err := r.rdb.Eval(ctx, releaseLockLua, []string{lockKey}, token).Result()
	if err != nil {
		return false, err
	}
	val, ok := res.(int64)
	return ok && val == 1, nil
}
