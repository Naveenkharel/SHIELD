#!/usr/bin/env bash
set -euo pipefail

LOG_DIR="$(cd "$(dirname "$0")/.." && pwd)/logs"

if compgen -G "$LOG_DIR"/env_*.pid > /dev/null; then
    echo "Stopping environment(s)..."
    for pid_file in "$LOG_DIR"/env_*.pid; do
        pid=$(cat "$pid_file")
        if kill -0 "$pid" 2>/dev/null; then
            echo "  stopping PID $pid (from $(basename "$pid_file"))"
            kill -TERM "$pid" 2>/dev/null || true
            # Wait briefly for clean exit
            for i in $(seq 1 10); do
                if ! kill -0 "$pid" 2>/dev/null; then
                    break
                fi
                sleep 0.5
            done

            # Force kill if still alive
            if kill -0 "$pid" 2>/dev/null; then
                echo "  force killing PID $pid"
                kill -9 "$pid" 2>/dev/null || true
            fi
        fi
        rm -f "$pid_file"
    done
    echo "All environment(s) stopped."
else
    echo "No environment PID files found."
fi