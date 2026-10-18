package main

import (
	"context"
	"fmt"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"
	"time"

	"mc-cluster/services/ender-vault/internal/handler"
	"mc-cluster/services/ender-vault/internal/repository"
	"mc-cluster/services/ender-vault/internal/service"

	"github.com/gin-gonic/gin"
	"github.com/redis/go-redis/v9"
	"gopkg.in/yaml.v3"
	"gorm.io/driver/mysql"
	"gorm.io/gorm"
	"gorm.io/gorm/logger"
)

type Config struct {
	Server struct {
		Port         int           `yaml:"port"`
		Mode         string        `yaml:"mode"`
		ReadTimeout  time.Duration `yaml:"read_timeout"`
		WriteTimeout time.Duration `yaml:"write_timeout"`
	} `yaml:"server"`
	Database struct {
		Driver          string        `yaml:"driver"`
		DSN             string        `yaml:"dsn"`
		MaxIdleConns    int           `yaml:"max_idle_conns"`
		MaxOpenConns    int           `yaml:"max_open_conns"`
		ConnMaxLifetime time.Duration `yaml:"conn_max_lifetime"`
	} `yaml:"database"`
	Redis struct {
		Addr         string `yaml:"addr"`
		Password     string `yaml:"password"`
		DB           int    `yaml:"db"`
		PoolSize     int    `yaml:"pool_size"`
		MinIdleConns int    `yaml:"min_idle_conns"`
	} `yaml:"redis"`
	BatchWriter struct {
		ChannelBufferSize int `yaml:"channel_buffer_size"`
		BatchSize         int `yaml:"batch_size"`
		FlushIntervalMs   int `yaml:"flush_interval_ms"`
	} `yaml:"batch_writer"`
}

func loadConfig(path string) (*Config, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var cfg Config
	if err := yaml.Unmarshal(data, &cfg); err != nil {
		return nil, err
	}
	// 支持环境变量覆盖
	if envDSN := os.Getenv("MYSQL_DSN"); envDSN != "" {
		cfg.Database.DSN = envDSN
	}
	if envRedis := os.Getenv("REDIS_ADDR"); envRedis != "" {
		cfg.Redis.Addr = envRedis
	}
	return &cfg, nil
}

func main() {
	log.Println("==================================================")
	log.Println(" Starting EnderVault Data Proxy Service (Golang) ")
	log.Println("==================================================")

	configPath := "config/config.yaml"
	if customPath := os.Getenv("CONFIG_PATH"); customPath != "" {
		configPath = customPath
	}

	cfg, err := loadConfig(configPath)
	if err != nil {
		log.Printf("Warning: failed to read %s, using defaults: %v", configPath, err)
		cfg = &Config{}
		cfg.Server.Port = 8080
		cfg.Server.Mode = "release"
		cfg.Database.DSN = "mc_user:mc_cluster_secret@(127.0.0.1:3306)/mc_cluster?charset=utf8mb4&parseTime=True&loc=Local"
		cfg.Redis.Addr = "127.0.0.1:6379"
		cfg.BatchWriter.ChannelBufferSize = 10000
		cfg.BatchWriter.BatchSize = 100
		cfg.BatchWriter.FlushIntervalMs = 200
	}

	gin.SetMode(cfg.Server.Mode)

	// 1. 初始化 MySQL GORM
	log.Printf("Connecting to MySQL: %s", cfg.Database.DSN)
	db, err := gorm.Open(mysql.Open(cfg.Database.DSN), &gorm.Config{
		Logger: logger.Default.LogMode(logger.Warn),
	})
	if err != nil {
		log.Printf("[EnderVault] Warning: MySQL connection error: %v (retrying in background)", err)
	} else {
		sqlDB, _ := db.DB()
		if cfg.Database.MaxIdleConns > 0 {
			sqlDB.SetMaxIdleConns(cfg.Database.MaxIdleConns)
		}
		if cfg.Database.MaxOpenConns > 0 {
			sqlDB.SetMaxOpenConns(cfg.Database.MaxOpenConns)
		}
		if cfg.Database.ConnMaxLifetime > 0 {
			sqlDB.SetConnMaxLifetime(cfg.Database.ConnMaxLifetime)
		}
		log.Println("[EnderVault] MySQL connected successfully.")
	}

	// 2. 初始化 Redis Client
	log.Printf("Connecting to Redis: %s", cfg.Redis.Addr)
	rdb := redis.NewClient(&redis.Options{
		Addr:         cfg.Redis.Addr,
		Password:     cfg.Redis.Password,
		DB:           cfg.Redis.DB,
		PoolSize:     cfg.Redis.PoolSize,
		MinIdleConns: cfg.Redis.MinIdleConns,
	})

	// 3. 构建仓储与业务服务层
	repo := repository.NewRepository(db, rdb)
	svc := service.NewPlayerService(repo, cfg.BatchWriter.ChannelBufferSize)

	// 4. 启动 Channel 异步写协程池
	flushInterval := time.Duration(cfg.BatchWriter.FlushIntervalMs) * time.Millisecond
	svc.StartBatchWorker(context.Background(), cfg.BatchWriter.BatchSize, flushInterval)

	// 5. 初始化 HTTP 路由
	router := gin.Default()
	h := handler.NewPlayerHandler(repo, svc)
	h.RegisterRoutes(router)

	srv := &http.Server{
		Addr:         fmt.Sprintf(":%d", cfg.Server.Port),
		Handler:      router,
		ReadTimeout:  10 * time.Second,
		WriteTimeout: 10 * time.Second,
	}

	// 异步监听
	go func() {
		log.Printf("[EnderVault] Server listening on :%d", cfg.Server.Port)
		if err := srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Fatalf("[EnderVault] Listen failed: %s\n", err)
		}
	}()

	// 6. 优雅关机监听
	quit := make(chan os.Signal, 1)
	signal.Notify(quit, syscall.SIGINT, syscall.SIGTERM)
	<-quit
	log.Println("[EnderVault] Shutting down server gracefully...")

	// 停止批处理并刷空 Channel 缓冲区
	svc.StopBatchWorker()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if err := srv.Shutdown(ctx); err != nil {
		log.Fatalf("[EnderVault] Server forced to shutdown: %v", err)
	}

	if rdb != nil {
		_ = rdb.Close()
	}

	log.Println("[EnderVault] Server exited successfully.")
}
