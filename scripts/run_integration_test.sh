#!/bin/bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "[*] Cleaning up old server instances..."
pkill -9 -f bin/server || true
sleep 0.5

echo "[*] Starting MCPE server in background..."
./bin/server > /tmp/mcpe_server.log 2>&1 &
SERVER_PID=$!
sleep 1

# Ensure server cleanup on exit
cleanup() {
    echo "[*] Stopping server (PID: $SERVER_PID)..."
    kill -9 $SERVER_PID 2>/dev/null || true
    pkill -9 -f bin/server 2>/dev/null || true
}
trap cleanup EXIT

echo "[*] Running Phase 4 Lobby & AOI integration test..."
python3 scripts/test_lobby_integration.py 25566 127.0.0.1

echo "[*] Running Sprint 3 multiplayer regression test..."
python3 scripts/test_sprint3_multiplayer.py

echo "[+] All integration tests finished successfully!"
