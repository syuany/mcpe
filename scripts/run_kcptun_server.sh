#!/usr/bin/env bash
# ==============================================================================
# MCPE KCP Acceleration Tunnel Server Runner (via Docker + GOST)
# ==============================================================================
set -euo pipefail

CONTAINER_NAME="mcpe-kcp-tunnel"
LISTEN_PORT="29900"
TARGET_PORT="25566"

echo "[*] Ensuring previous container is cleaned up..."
docker rm -f "${CONTAINER_NAME}" 2>/dev/null || true

echo "[*] Launching GOST KCP server container on UDP :${LISTEN_PORT} -> TCP 127.0.0.1:${TARGET_PORT}..."
docker run -d \
  --name "${CONTAINER_NAME}" \
  --restart=always \
  --net=host \
  ginuerzh/gost \
  -L="kcp://:${LISTEN_PORT}/127.0.0.1:${TARGET_PORT}?mode=fast3&nodelay=1"

echo "[+] KCP tunnel server is active and listening on UDP :${LISTEN_PORT}"
docker ps --filter "name=${CONTAINER_NAME}"
