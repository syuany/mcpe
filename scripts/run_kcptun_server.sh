#!/usr/bin/env bash
# ==============================================================================
# MCPE KCP Acceleration Tunnel Server Runner (via Docker)
# ==============================================================================
set -euo pipefail

CONTAINER_NAME="mcpe-kcptun-server"
LISTEN_PORT="29900"
TARGET_PORT="25566"

echo "[*] Ensuring previous container is cleaned up..."
docker rm -f "${CONTAINER_NAME}" 2>/dev/null || true

echo "[*] Launching kcptun server container on UDP :${LISTEN_PORT} -> TCP 127.0.0.1:${TARGET_PORT}..."
docker run -d \
  --name "${CONTAINER_NAME}" \
  --restart=always \
  --net=host \
  xtaci/kcptun server \
  -t "127.0.0.1:${TARGET_PORT}" \
  -l ":${LISTEN_PORT}" \
  -mode fast3 \
  -crypt none \
  -nocomp \
  -datashard 10 \
  -parityshard 3 \
  -mtu 1350 \
  -sndwnd 1024 \
  -rcvwnd 1024

echo "[+] kcptun server is active and listening on UDP :${LISTEN_PORT}"
docker ps --filter "name=${CONTAINER_NAME}"
