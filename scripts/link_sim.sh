#!/usr/bin/env bash
set -euo pipefail

NS3_ROOT="${NS3_ROOT:-$HOME/ns-3-45}"
REPO_ROOT="$(git rev-parse --show-toplevel)"

if [ ! -d "$NS3_ROOT/scratch" ]; then
  echo "ERROR: $NS3_ROOT/scratch not found — check NS3_ROOT points at your ns-3.45 build dir." >&2
  exit 1
fi
if [ ! -d "$REPO_ROOT/sim/ns3" ]; then
  echo "ERROR: $REPO_ROOT/sim/ns3 not found — run this from inside the SHIELD repo." >&2
  exit 1
fi

ln -sfn "$REPO_ROOT/sim/ns3" "$NS3_ROOT/scratch/shield"
echo "Linked $REPO_ROOT/sim/ns3 -> $NS3_ROOT/scratch/shield"

if [ ! -f "$REPO_ROOT/sim/tcp_bridge/tcp_bridge_server.cc" ]; then
  echo "ERROR: $REPO_ROOT/sim/tcp_bridge/tcp_bridge_server.cc not found." >&2
  exit 1
fi

ln -sfn "$REPO_ROOT/sim/tcp_bridge/tcp_bridge_server.cc" "$NS3_ROOT/scratch/tcp_bridge_server.cc"
echo "Linked $REPO_ROOT/sim/tcp_bridge/tcp_bridge_server.cc -> $NS3_ROOT/scratch/tcp_bridge_server.cc"