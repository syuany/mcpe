package handler

import (
	"net/http"
	"strconv"

	"mc-cluster/services/ender-vault/internal/model"
	"mc-cluster/services/ender-vault/internal/repository"
	"mc-cluster/services/ender-vault/internal/service"

	"github.com/gin-gonic/gin"
)

type PlayerHandler struct {
	repo repository.Repository
	svc  service.PlayerService
}

func NewPlayerHandler(repo repository.Repository, svc service.PlayerService) *PlayerHandler {
	return &PlayerHandler{
		repo: repo,
		svc:  svc,
	}
}

func (h *PlayerHandler) RegisterRoutes(r *gin.Engine) {
	// 健康检查
	r.GET("/health", h.HealthCheck)

	// API v1 分组
	v1 := r.Group("/api/v1")
	{
		// 玩家数据跨服代理
		v1.GET("/player/:uuid/data", h.GetPlayerData)
		v1.POST("/player/:uuid/data", h.UpdatePlayerData)

		// 跑酷通关与实时排行榜
		v1.POST("/parkour/finish", h.RecordParkourFinish)
		v1.GET("/parkour/leaderboard", h.GetLeaderboard)

		// 分布式互斥锁
		v1.POST("/lock/acquire", h.AcquireLock)
		v1.POST("/lock/release", h.ReleaseLock)
	}
}

func (h *PlayerHandler) HealthCheck(c *gin.Context) {
	ctx := c.Request.Context()
	dbStatus := "connected"
	if err := h.repo.PingDB(ctx); err != nil {
		dbStatus = "error: " + err.Error()
	}

	redisStatus := "connected"
	if err := h.repo.PingRedis(ctx); err != nil {
		redisStatus = "error: " + err.Error()
	}

	statusCode := http.StatusOK
	if dbStatus != "connected" || redisStatus != "connected" {
		statusCode = http.StatusServiceUnavailable
	}

	c.JSON(statusCode, gin.H{
		"service":  "ender-vault",
		"status":   "ok",
		"database": dbStatus,
		"redis":    redisStatus,
	})
}

func (h *PlayerHandler) GetPlayerData(c *gin.Context) {
	uuid := c.Param("uuid")
	username := c.Query("username")

	data, err := h.svc.GetPlayerData(c.Request.Context(), uuid, username)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": err.Error(),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
		"data":    data,
	})
}

func (h *PlayerHandler) UpdatePlayerData(c *gin.Context) {
	uuid := c.Param("uuid")
	var req model.UpdatePlayerRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": "invalid request body: " + err.Error(),
		})
		return
	}

	if err := h.svc.UpdatePlayerData(c.Request.Context(), uuid, &req); err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": err.Error(),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":    0,
		"message": "success",
	})
}

func (h *ParkourFinishHandler) dummy() {}

func (h *PlayerHandler) RecordParkourFinish(c *gin.Context) {
	var req model.ParkourFinishRequest
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{
			"code":    400,
			"message": err.Error(),
		})
		return
	}

	isNewRecord, rank, rewardCoins, err := h.svc.RecordParkour(c.Request.Context(), &req)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": err.Error(),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":          0,
		"message":       "recorded",
		"is_new_record": isNewRecord,
		"rank":          rank,
		"reward_coins":  rewardCoins,
	})
}

type ParkourFinishHandler struct{}

func (h *PlayerHandler) GetLeaderboard(c *gin.Context) {
	trackID := c.DefaultQuery("track_id", "rooftop_sprint")
	limitStr := c.DefaultQuery("limit", "10")
	limit, _ := strconv.Atoi(limitStr)

	entries, err := h.svc.GetLeaderboard(c.Request.Context(), trackID, limit)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{
			"code":    500,
			"message": err.Error(),
		})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"code":     0,
		"message":  "success",
		"track_id": trackID,
		"entries":  entries,
	})
}

func (h *PlayerHandler) AcquireLock(c *gin.Context) {
	var req struct {
		ResourceKey string `json:"resource_key" binding:"required"`
		TTLSeconds  int    `json:"ttl_seconds"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"code": 400, "message": err.Error()})
		return
	}

	token, acquired, err := h.svc.AcquireLock(c.Request.Context(), req.ResourceKey, req.TTLSeconds)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"code": 500, "message": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"acquired":   acquired,
		"lock_token": token,
	})
}

func (h *PlayerHandler) ReleaseLock(c *gin.Context) {
	var req struct {
		ResourceKey string `json:"resource_key" binding:"required"`
		LockToken   string `json:"lock_token" binding:"required"`
	}
	if err := c.ShouldBindJSON(&req); err != nil {
		c.JSON(http.StatusBadRequest, gin.H{"code": 400, "message": err.Error()})
		return
	}

	success, err := h.svc.ReleaseLock(c.Request.Context(), req.ResourceKey, req.LockToken)
	if err != nil {
		c.JSON(http.StatusInternalServerError, gin.H{"code": 500, "message": err.Error()})
		return
	}

	c.JSON(http.StatusOK, gin.H{
		"success": success,
	})
}
