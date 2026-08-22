#!/usr/bin/env bash
set -euo pipefail

# Usage: ./launch_parallel_envs.sh <N>
N="${1:?Usage: launch_parallel_envs.sh <N>}"

BASE_PORT=9999
BIN="${BIN:-/home/keshav/ns-3-45/build/scratch/ns3.45-tcp_bridge_server-optimized}"
LOG_DIR="$(cd "$(dirname "$0")/.." && pwd)/logs"

mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/env_*.pid

echo "Launching $N environment(s)..."

for ((i=0; i<N; i++)); do
    port=$((BASE_PORT + i))
    log="$LOG_DIR/env_${i}.log"
    pid_file="$LOG_DIR/env_${i}.pid"

    nohup "$BIN" --port "$port" > "$log" 2>&1 &
    pid=$!
    echo "$pid" > "$pid_file"

    ready=0
    for attempt in $(seq 1 20); do
        if grep -q "listening on port $port" "$log" 2>/dev/null; then
            ready=1
            break
        fi
        sleep 0.5
    done

    if [[ "$ready" -eq 1 ]]; then
        echo "  env_$i : port $port (PID $pid) ready"
    else
        echo "  env_$i : FAILED to start (see $log)" >&2
        kill "$pid" 2>/dev/null || true
        rm -f "$pid_file"
        exit 1
    fi
done

echo "All $N environment(s) are ready."