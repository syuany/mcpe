#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"
pkill -9 -f bin/server || true
sleep 0.5
nohup ./bin/server > /tmp/mcpe_server.log 2>&1 &
disown
sleep 0.5
echo "[+] MCPE server started with PID $(pgrep -f bin/server)"
