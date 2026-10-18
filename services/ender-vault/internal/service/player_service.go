package service

import (
	"context"
	"fmt"
	"log"
	"math/rand"
	"sync"
	"time"

	"mc-cluster/services/ender-vault/internal/model"
	"mc-cluster/services/ender-vault/internal/repository"

	"golang.org/x/sync/singleflight"
)

// PlayerService 核心业务接口
type PlayerService interface {
	GetPlayerData(ctx context.Context, uuid, username string) (*model.PlayerFullData, error)
	UpdatePlayerData(ctx context.Context, uuid string, req *model.UpdatePlayerRequest) error
	RecordParkour(ctx context.Context, req *model.ParkourFinishRequest) (bool, uint64, int64, error)
	GetLeaderboard(ctx context.Context, trackID string, limit int) ([]model.LeaderboardEntry, error)
	AcquireLock(ctx context.Context, resourceKey string, ttlSeconds int) (string, bool, error)
	ReleaseLock(ctx context.Context, resourceKey, token string) (bool, error)

	// 批处理启动与停止
	StartBatchWorker(ctx context.Context, batchSize int, flushInterval time.Duration)
	StopBatchWorker()
}

type playerServiceImpl struct {
	repo         repository.Repository
	sfGroup      singleflight.Group
	batchTaskCh  chan *model.UpdateBatchTask
	stopCh       chan struct{}
	wg           sync.WaitGroup
	cacheTTLBase time.Duration
}

func NewPlayerService(repo repository.Repository, channelBuffer int) PlayerService {
	return &playerServiceImpl{
		repo:         repo,
		batchTaskCh:  make(chan *model.UpdateBatchTask, channelBuffer),
		stopCh:       make(chan struct{}),
		cacheTTLBase: 10 * time.Minute,
	}
}

// GetPlayerData 读接口：Redis 缓存 -> Singleflight 防穿透/防击穿 -> MySQL 回源 -> 回填 Redis
func (s *playerServiceImpl) GetPlayerData(ctx context.Context, uuid, username string) (*model.PlayerFullData, error) {
	// 1. 尝试从 Redis 缓存获取
	cached, err := s.repo.GetPlayerCache(ctx, uuid)
	if err == nil && cached != nil {
		return cached, nil
	}

	// 2. 缓存未命中，使用 Singleflight 合并相同 UUID 的并发回源请求
	v, err, _ := s.sfGroup.Do(fmt.Sprintf("get_player_%s", uuid), func() (interface{}, error) {
		// 二次校验缓存 (Double Check)
		if doubleCheck, _ := s.repo.GetPlayerCache(ctx, uuid); doubleCheck != nil {
			return doubleCheck, nil
		}

		player, err := s.repo.GetPlayer(ctx, uuid)
		var vault *model.PlayerVault

		if err != nil {
			// 如果不存在且携带了 username，则自动注册
			if username != "" {
				player, vault, err = s.repo.CreatePlayerIfNotExist(ctx, uuid, username)
				if err != nil {
					return nil, fmt.Errorf("failed to auto-create player: %w", err)
				}
			} else {
				return nil, err
			}
		} else {
			vault, _ = s.repo.GetVault(ctx, uuid)
		}

		fullData := &model.PlayerFullData{
			UUID:       player.UUID,
			Username:   player.Username,
			Coins:      player.Coins,
			Level:      player.Level,
			Exp:        player.Exp,
			LastServer: player.LastServer,
		}

		if vault != nil {
			fullData.InventoryData = vault.InventoryData
			fullData.EnderChestData = vault.EnderChestData
			fullData.ExtraAttributes = vault.ExtraAttributes
			fullData.Version = vault.Version
		}

		// 回填缓存，随机浮动 0~120 秒，防雪崩
		jitter := time.Duration(rand.Intn(120)) * time.Second
		_ = s.repo.SetPlayerCache(ctx, fullData, s.cacheTTLBase+jitter)

		return fullData, nil
	})

	if err != nil {
		return nil, err
	}
	return v.(*model.PlayerFullData), nil
}

// UpdatePlayerData 写接口：支持乐观锁和 Channel 异步批处理合并写
func (s *playerServiceImpl) UpdatePlayerData(ctx context.Context, uuid string, req *model.UpdatePlayerRequest) error {
	// 如果涉及背包结构或版本号 CAS，走同步数据库事务并清除缓存
	if req.InventoryData != "" || req.EnderChestData != "" || req.ExtraAttributes != "" || req.ExpectedVersion > 0 {
		if err := s.repo.UpdateVaultData(ctx, uuid, req); err != nil {
			return err
		}
		// 淘汰缓存，保证强一致性
		_ = s.repo.InvalidatePlayerCache(ctx, uuid)
		return nil
	}

	// 纯数值/状态轻量变动（硬币、经验、所在服务器），投递至 Channel 异步微批处理
	select {
	case s.batchTaskCh <- &model.UpdateBatchTask{
		UUID:       uuid,
		CoinsDelta: req.CoinsDelta,
		ExpDelta:   req.ExpDelta,
		LastServer: req.LastServer,
	}:
		// 淘汰缓存，后续读取会回源
		_ = s.repo.InvalidatePlayerCache(ctx, uuid)
		return nil
	default:
		// 通道满载降级为同步写
		log.Printf("[EnderVault] Warning: batch channel is full, fallback to sync write for %s", uuid)
		err := s.repo.UpdateVaultData(ctx, uuid, req)
		_ = s.repo.InvalidatePlayerCache(ctx, uuid)
		return err
	}
}

// RecordParkour 跑酷冲线结算业务
func (s *playerServiceImpl) RecordParkour(ctx context.Context, req *model.ParkourFinishRequest) (bool, uint64, int64, error) {
	record := &model.ParkourRecord{
		PlayerUUID:  req.PlayerUUID,
		PlayerName:  req.PlayerName,
		TrackID:     req.TrackID,
		TimeMs:      req.TimeMs,
		Checkpoints: req.Checkpoints,
		CreatedAt:   time.Now(),
	}

	// 1. MySQL 持久化历史明细
	if err := s.repo.SaveParkourRecord(ctx, record); err != nil {
		return false, 0, 0, err
	}

	// 2. 更新 Redis ZSet 实时榜单
	_ = s.repo.AddLeaderboardCache(ctx, req.TrackID, req.PlayerUUID, req.PlayerName, req.TimeMs)

	// 3. 通关金币奖励 (异步累加 100 代币)
	rewardCoins := int64(100)
	_ = s.UpdatePlayerData(ctx, req.PlayerUUID, &model.UpdatePlayerRequest{
		CoinsDelta: rewardCoins,
		ExpDelta:   200,
	})

	return true, 1, rewardCoins, nil
}

// GetLeaderboard 获取跑酷实时榜单
func (s *playerServiceImpl) GetLeaderboard(ctx context.Context, trackID string, limit int) ([]model.LeaderboardEntry, error) {
	if limit <= 0 || limit > 100 {
		limit = 10
	}
	return s.repo.GetLeaderboard(ctx, trackID, limit)
}

// AcquireLock 分布式锁
func (s *playerServiceImpl) AcquireLock(ctx context.Context, resourceKey string, ttlSeconds int) (string, bool, error) {
	if ttlSeconds <= 0 {
		ttlSeconds = 5
	}
	token := fmt.Sprintf("%d_%d", time.Now().UnixNano(), rand.Int63())
	ok, err := s.repo.AcquireLock(ctx, resourceKey, token, time.Duration(ttlSeconds)*time.Second)
	if err != nil || !ok {
		return "", false, err
	}
	return token, true, nil
}

// ReleaseLock 释放分布式锁
func (s *playerServiceImpl) ReleaseLock(ctx context.Context, resourceKey, token string) (bool, error) {
	return s.repo.ReleaseLock(ctx, resourceKey, token)
}

// StartBatchWorker 启动后台 Channel 批处理落盘 Worker
func (s *playerServiceImpl) StartBatchWorker(ctx context.Context, batchSize int, flushInterval time.Duration) {
	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		ticker := time.NewTicker(flushInterval)
		defer ticker.Stop()

		var buffer []*model.UpdateBatchTask

		flush := func() {
			if len(buffer) == 0 {
				return
			}
			flushCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
			defer cancel()

			if err := s.repo.BatchFlushPlayers(flushCtx, buffer); err != nil {
				log.Printf("[EnderVault] Error batch flushing %d player updates: %v", len(buffer), err)
			} else {
				log.Printf("[EnderVault] Successfully flushed batch of %d updates to MySQL", len(buffer))
			}
			buffer = buffer[:0]
		}

		for {
			select {
			case <-s.stopCh:
				flush()
				return
			case task := <-s.batchTaskCh:
				buffer = append(buffer, task)
				if len(buffer) >= batchSize {
					flush()
				}
			case <-ticker.C:
				flush()
			}
		}
	}()
}

func (s *playerServiceImpl) StopBatchWorker() {
	close(s.stopCh)
	s.wg.Wait()
}
