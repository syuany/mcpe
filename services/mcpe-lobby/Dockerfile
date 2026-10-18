# ==============================================================================
# Stage 1: 编译构建镜像 (Builder)
# ==============================================================================
FROM debian:bookworm-slim AS builder

# 安装现代 C++23 编译环境与工具链
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    gcc-14 \
    g++-14 \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# 设置默认编译器为 GCC 14
ENV CC=gcc-14
ENV CXX=g++-14

WORKDIR /workspace
COPY . .

# 执行 CMake 配置并开启 Release 优化
RUN cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target server -j$(nproc)

# 剔除二进制冗余符号表，极致压缩二进制体积
RUN strip --strip-all /workspace/bin/server

# ==============================================================================
# Stage 2: 生产运行镜像 (Runtime) - 极致轻量 (≤ 30MB)
# ==============================================================================
FROM debian:bookworm-slim AS runtime

# 安装仅有的运行时依赖 (libstdc++6) 并创建非 root 运行账号
RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && useradd -u 1000 -m -s /bin/bash mcpe

WORKDIR /app

# 从构建镜像中仅提取最终二进制与必要资产
COPY --from=builder /workspace/bin/server /app/bin/server
COPY --from=builder /workspace/src/settings.ini /app/src/settings.ini
COPY --from=builder /workspace/assets /app/assets

# 赋权并切换至非 root 用户，杜绝容器逃逸安全隐患
RUN chown -R mcpe:mcpe /app
USER mcpe

# 暴露 Minecraft 服务端通信端口
EXPOSE 25566

# 使用 exec 模式启动，保证正确接管 PID 1 信号 (SIGINT/SIGTERM 优雅停机)
ENTRYPOINT ["/app/bin/server"]
