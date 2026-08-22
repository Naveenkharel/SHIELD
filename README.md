# SHIELD

SHIELD is an ns-3-based network security simulation for training and evaluating
defense policies against network attacks. It includes a simulation controller,
attack traffic models, telemetry and reward calculation, and a TCP bridge for
external agents.

## Requirements

- Ubuntu or another Linux distribution with Bash
- Python 3
- A C++ toolchain and the dependencies required by ns-3
- ns-3.45

The setup script installs the Ubuntu build dependencies and builds ns-3.45 in
`$HOME/ns-3-45` by default. It requires `sudo` for installing packages and an
internet connection for downloading ns-3.

## Setup

From the repository root:

```bash
./scripts/setup_ns3.sh
./scripts/link_sim.sh
```

To install ns-3 somewhere else, set `NS3_ROOT` consistently for both commands:

```bash
NS3_ROOT=/path/to/ns-3-45 ./scripts/setup_ns3.sh
NS3_ROOT=/path/to/ns-3-45 ./scripts/link_sim.sh
```

The link script makes the project simulation sources available under ns-3's
`scratch/` directory without copying them into the repository.

## Build and run the TCP bridge

Build the linked sources from the ns-3 directory:

```bash
cd "$HOME/ns-3-45"
./ns3 build
cd /path/to/SHIELD
```

Start one environment on the default port, 9999:

```bash
BIN="$HOME/ns-3-45/build/scratch/ns3.45-tcp_bridge_server-optimized" \
  ./scripts/launch_parallel_envs.sh 1
```

The launcher supports multiple environments on ports 9999, 10000, and so on.
Set `BIN` if your ns-3 build produces the bridge binary at another path.

Stop all environments started by the launcher with:

```bash
./scripts/stop_parallel_envs.sh
```

## TCP protocol

The bridge accepts one JSON object per line. A client must reset before sending
steps:

```json
{"cmd":"reset"}
{"cmd":"step","actions":[0,0,0,0,0,0,0,0,0,0,0,0,0,0]}
{"cmd":"close"}
```

`reset` and `step` return an observation with 14 nodes and 12 features per
node. A step response also includes `reward` and `done`; an episode contains
200 steps.

## Tests

With the bridge running on port 9999, run the client checks from another
terminal:

```bash
python3 tests/test_tcp_client.py
python3 tests/test_unexpected_disconnect.py
```

The ns-3 simulation executable also contains reproducibility, benchmark, and
observation-shape checks. Run those through the ns-3 build after linking the
sources.

## Repository layout

```text
agents/          Agent code and integrations
anomaly/         Anomaly-detection components
configs/         Experiment configuration files
dashboard/       Dashboard code
evaluation/      Evaluation utilities and results
explainability/  Explainability components
scripts/         ns-3 setup, linking, and environment management
sim/ns3/         Core ns-3 simulation sources
sim/tcp_bridge/  TCP server that exposes the simulation to clients
tests/           Python integration and disconnect tests
```
