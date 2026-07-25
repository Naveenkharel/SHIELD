#!/usr/bin/env python3
import socket
import json
import random

HOST = "127.0.0.1"
PORT = 9999

def send_and_recv(sock, msg):
    """Send a JSON message as a line, receive a line and parse it."""
    data = json.dumps(msg) + "\n"
    sock.sendall(data.encode())
    buf = b""
    while not buf.endswith(b"\n"):
        buf += sock.recv(1024)
    return json.loads(buf.decode().strip())

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))

    # 1. Reset
    print("Sending reset...")
    resp = send_and_recv(sock, {"cmd": "reset"})
    obs = resp["obs"]
    assert len(obs) == 14, f"Expected 14 nodes, got {len(obs)}"
    for i, row in enumerate(obs):
        assert len(row) == 12, f"Node {i} has {len(row)} features instead of 12"
    print("Reset OK. Observation shape 14x12.")

    # 2. Five step commands with random actions
    for step in range(5):
        actions = [random.randint(0, 4) for _ in range(14)]
        print(f"Step {step}: actions={actions}")
        resp = send_and_recv(sock, {"cmd": "step", "actions": actions})
        obs = resp["obs"]
        reward = resp["reward"]
        done = resp["done"]
        assert isinstance(reward, (int, float)), f"reward is not a number: {reward}"
        assert isinstance(done, bool), f"done is not a bool: {done}"
        print(f"  reward={reward}, done={done}")

    # 3. Close
    print("Sending close...")
    send_and_recv(sock, {"cmd": "close"})
    sock.close()
    print("Test completed successfully.")

if __name__ == "__main__":
    main()