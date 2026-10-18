# ==============================================================================
# Stage 1: 编译构建镜像 (Builder)
# ==============================================================================
FROM golang:1.22-alpine AS builder

WORKDIR /app

# 配置 Go 代理并预装证书
ENV GOPROXY=https://goproxy.cn,direct
RUN apk add --no-cache git ca-certificates

# 拷贝依赖定义并下载
COPY go.mod go.sum* ./
RUN go mod download || true

# 拷贝源代码并执行静态编译
COPY . .
RUN CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build \
    -ldflags="-s -w -extldflags '-static'" \
    -o /app/bin/ender-vault ./cmd/server/main.go

# ==============================================================================
# Stage 2: 极致轻量生产运行镜像 (Runtime)
# ==============================================================================
FROM alpine:3.19 AS runtime

RUN apk add --no-cache ca-certificates tzdata curl && \
    addgroup -S appgroup && adduser -S appuser -G appgroup

WORKDIR /app

# 从构建阶段提取静态二进制与默认配置文件
COPY --from=builder /app/bin/ender-vault /app/ender-vault
COPY --from=builder /app/config /app/config

# 设置所属权限
RUN chown -R appuser:appgroup /app
USER appuser

EXPOSE 8080

HEALTHCHECK --interval=10s --timeout=3s --retries=3 \
  CMD curl -f http://localhost:8080/health || exit 1

ENTRYPOINT ["/app/ender-vault"]
