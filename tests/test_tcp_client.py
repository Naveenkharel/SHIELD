#!/usr/bin/env python3
"""Extended TCP test client for the ns‑3 bridge server."""

import socket
import json
import random
import sys

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9999

def send_and_recv(sock, msg, compact=True):
    """Send a JSON message as a line and return the parsed response."""
    if compact:
        data = json.dumps(msg, separators=(',', ':')) + "\n"
    else:
        data = json.dumps(msg) + "\n"   # default: spaces after commas
    sock.sendall(data.encode())
    buf = b""
    while not buf.endswith(b"\n"):
        buf += sock.recv(1024)
    return json.loads(buf.decode().strip())

def test():
    passed = True

    # ----- Quick 5‑step check (original) -----
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    resp = send_and_recv(sock, {"cmd": "reset"})
    obs = resp["obs"]
    if len(obs) != 14:
        print("FAIL: reset obs wrong size")
        passed = False
    else:
        for i, row in enumerate(obs):
            if len(row) != 12:
                print(f"FAIL: node {i} features {len(row)}")
                passed = False
        if passed:
            print("Reset OK – obs 14x12.")
    for step in range(5):
        actions = [random.randint(0, 4) for _ in range(14)]
        resp = send_and_recv(sock, {"cmd": "step", "actions": actions})
        if "error" in resp:
            print(f"FAIL: step {step} error: {resp['error']}")
            passed = False
        else:
            print(f"  step {step}: reward={resp['reward']}, done={resp['done']}")

    # ----- Test default JSON (spaces after commas) -----
    print("\nTesting step with default JSON (spaces after commas)...")
    resp = send_and_recv(sock, {"cmd": "step", "actions": [0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3]},
                         compact=False)
    if "error" in resp:
        print(f"FAIL: default JSON error: {resp['error']}")
        passed = False
    else:
        print("OK – default JSON accepted.")

    send_and_recv(sock, {"cmd": "close"})
    sock.close()

    # ----- Full 200‑step episode -----
    print("\nFull 200‑step episode...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    send_and_recv(sock, {"cmd": "reset"})
    done_flags = []
    for step in range(200):
        actions = [random.randint(0, 4) for _ in range(14)]
        resp = send_and_recv(sock, {"cmd": "step", "actions": actions})
        done_flags.append(resp["done"])
    for i, d in enumerate(done_flags):
        if i < 199 and d:
            print(f"FAIL: step {i} done=True prematurely")
            passed = False
        elif i == 199 and not d:
            print("FAIL: step 199 done=False")
            passed = False
    if passed:
        print("200-step episode: done flags correct.")

    # ----- Reset after full episode -----
    print("Reset after full episode...")
    send_and_recv(sock, {"cmd": "reset"})
    resp = send_and_recv(sock, {"cmd": "step", "actions": [0]*14})
    if resp.get("done"):
        print("FAIL: first step after reset done=True")
        passed = False
    else:
        print(f"After reset: reward={resp['reward']}, done={resp['done']}")
    send_and_recv(sock, {"cmd": "close"})
    sock.close()

    # ----- Step before reset on new connection -----
    print("\nTesting step before reset...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((HOST, PORT))
    resp = send_and_recv(sock, {"cmd": "step", "actions": [0]*14})
    if resp.get("error") == "step called before reset":
        print("OK – expected error received.")
    else:
        print(f"FAIL: expected error, got {resp}")
        passed = False
    sock.close()

    print("\n" + ("PASS" if passed else "FAIL"))

if __name__ == "__main__":
    test()