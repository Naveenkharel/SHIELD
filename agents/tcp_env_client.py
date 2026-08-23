"""
agents/tcp_env_client.py

TCP client that connects to Keshav's ns-3 bridge server
(sim/tcp_bridge_server.cc) and exposes a Gym-style interface:

    obs = env.reset()
    next_obs, reward, done, info = env.step(actions)

This is the ONLY file that knows about sockets/JSON wire format. Nothing
else in agents/ (qmix_trainer.py, replay_buffer.py, etc.) needs to change
regardless of whether the environment is this TCP bridge or a future
Python-native mock -- they only ever see reset()/step() in Gym form.

Matches the protocol defined in ns3_qmix_tcp_protocol.md, and verified
against Keshav's actual server implementation (sim/tcp_bridge_server.cc):
    - newline-delimited JSON, one message per line
    - reset -> {"obs": [[...], ...]}                (14x12)
    - step  -> {"obs": [[...]], "reward": float, "done": bool}
    - step called before reset -> {"error": "..."} from server
    - close -> {"status": "closing"}
"""

import json
import socket

import numpy as np


class TCPEnvClient:
    """
    Gym-style client wrapping a TCP connection to the ns-3 bridge server.
    """

    def __init__(self, host: str, port: int = 9999, n_agents: int = 14, obs_dim: int = 12,
                 connect_timeout: float = 10.0):
        """
        Args:
            host: IP or hostname of Keshav's machine (or "localhost" if
                  using an SSH tunnel -- see protocol doc section 2).
            port: TCP port the ns-3 server is listening on (default 9999).
            n_agents: expected number of routers (14).
            obs_dim: expected per-agent observation size (12).
            connect_timeout: seconds to wait when establishing the connection.
        """
        self.host = host
        self.port = port
        self.n_agents = n_agents
        self.obs_dim = obs_dim

        self._sock = socket.create_connection((host, port), timeout=connect_timeout)
        # makefile() gives us a buffered, line-oriented read/write interface
        # over the raw socket -- this handles TCP's "may not deliver a full
        # message in one read()" behavior for us, matching the server's
        # ReadLine() logic on the C++ side.
        self._reader = self._sock.makefile("r", encoding="utf-8", newline="\n")
        self._writer = self._sock.makefile("w", encoding="utf-8", newline="\n")

        self._has_been_reset = False

    # ------------------------------------------------------------------
    # Low-level send/receive
    # ------------------------------------------------------------------
    def _send(self, message: dict):
        line = json.dumps(message) + "\n"
        self._writer.write(line)
        self._writer.flush()

    def _receive(self) -> dict:
        line = self._reader.readline()
        if line == "":
            raise ConnectionError(
                "ns-3 server closed the connection unexpectedly (empty read). "
                "Check the server is still running and the idle timeout wasn't hit."
            )
        response = json.loads(line)
        if "error" in response:
            raise RuntimeError(f"ns-3 server returned an error: {response['error']}")
        return response

    # ------------------------------------------------------------------
    # Gym-style interface
    # ------------------------------------------------------------------
    def reset(self) -> np.ndarray:
        """
        Start a new episode.

        Returns:
            obs: np.ndarray, shape (n_agents, obs_dim)
        """
        self._send({"cmd": "reset"})
        response = self._receive()

        obs = np.asarray(response["obs"], dtype=np.float32)
        self._validate_obs_shape(obs)

        self._has_been_reset = True
        return obs

    def step(self, actions):
        """
        Apply one joint action and advance the simulation by one cycle.

        Args:
            actions: list/array of n_agents ints, each in [0, 4].

        Returns:
            next_obs: np.ndarray, shape (n_agents, obs_dim)
            reward: float
            done: bool
            info: dict (always empty for this bridge)
        """
        if not self._has_been_reset:
            raise RuntimeError(
                "step() called before reset(). Call env.reset() at the start "
                "of every episode -- the server rejects step() otherwise."
            )

        actions_list = [int(a) for a in actions]
        if len(actions_list) != self.n_agents:
            raise ValueError(
                f"Expected {self.n_agents} actions, got {len(actions_list)}."
            )

        self._send({"cmd": "step", "actions": actions_list})
        response = self._receive()

        next_obs = np.asarray(response["obs"], dtype=np.float32)
        self._validate_obs_shape(next_obs)

        reward = float(response["reward"])
        done = bool(response["done"])

        if done:
            # Server resets its hasBeenReset flag after done=true (see
            # tcp_bridge_server.cc) -- mirror that here so our own
            # bookkeeping matches, and the next step() call fails fast
            # with a clear error if reset() is forgotten.
            self._has_been_reset = False

        info = {}
        return next_obs, reward, done, info

    def close(self):
        """Cleanly notify the server and close the socket."""
        try:
            self._send({"cmd": "close"})
            self._receive()  # expects {"status": "closing"}
        except Exception:
            pass  # best-effort -- still close the socket below regardless
        finally:
            self._reader.close()
            self._writer.close()
            self._sock.close()

    # ------------------------------------------------------------------
    # Helpers
    # ------------------------------------------------------------------
    def _validate_obs_shape(self, obs: np.ndarray):
        expected_shape = (self.n_agents, self.obs_dim)
        if obs.shape != expected_shape:
            raise ValueError(
                f"Received obs with shape {obs.shape}, expected {expected_shape}. "
                f"Check feature order / neighbor padding matches the protocol spec."
            )

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


# -----------------------------------------------------------------------
# Manual test against a real server.
#
# Usage:
#   python -m agents.tcp_env_client --host <keshav_ip_or_localhost> --port 9999
#
# Run this AFTER Keshav's tcp_bridge_server is running (with either the
# real sim or a stub controller behind it). This is not an automated
# pytest -- it's a quick manual smoke test you run by hand.
# -----------------------------------------------------------------------
if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=9999)
    args = parser.parse_args()

    env = TCPEnvClient(host=args.host, port=args.port)

    obs = env.reset()
    print("reset() obs shape:", obs.shape)
    print("first router's obs:", obs[0])

    for i in range(3):
        fake_actions = [0] * 14  # "Allow" for every router, just to test plumbing
        next_obs, reward, done, info = env.step(fake_actions)
        print(f"step {i}: next_obs shape={next_obs.shape}, reward={reward}, done={done}")
        if done:
            print("Episode ended, calling reset() again...")
            obs = env.reset()

    env.close()
    print("Closed cleanly.")