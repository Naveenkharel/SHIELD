#!/usr/bin/env python3
"""Verify the TCP bridge survives an unexpected client disconnect."""

import socket
import json
import time

HOST = "127.0.0.1"
PORT = 9999

def send_and_recv(sock, msg):
    data = json.dumps(msg, separators=(',', ':')) + "\n"
    sock.sendall(data.encode())
    buf = b""
    while not buf.endswith(b"\n"):
        buf += sock.recv(1024)
    return json.loads(buf.decode().strip())

def main():
    passed = True

    # ----- First connection: reset, then die abruptly -----
    print("Connecting (first client)...")
    s1 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s1.connect((HOST, PORT))

    print("Sending reset...")
    resp = send_and_recv(s1, {"cmd": "reset"})
    obs = resp.get("obs")
    if not obs or len(obs) != 14:
        print("FAIL: first reset did not return valid obs")
        passed = False
    else:
        print("Reset OK.")

    print("Killing connection abruptly (no 'close' message)...")
    s1.close()   # abrupt TCP FIN, no JSON "close"

    # Give the server a moment to detect the disconnect
    time.sleep(1.0)

    # ----- Second connection: should still work -----
    print("\nConnecting (second client)...")
    s2 = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s2.connect((HOST, PORT))

    print("Sending reset on second client...")
    resp = send_and_recv(s2, {"cmd": "reset"})
    obs = resp.get("obs")
    if not obs or len(obs) != 14:
        print("FAIL: second reset did not return valid obs")
        passed = False
    else:
        print("Reset OK on second client.")

    print("Sending one step...")
    actions = [0] * 14
    resp = send_and_recv(s2, {"cmd": "step", "actions": actions})
    if "error" in resp:
        print(f"FAIL: step returned error: {resp['error']}")
        passed = False
    else:
        print(f"Step OK: reward={resp['reward']}, done={resp['done']}")

    print("Sending close...")
    send_and_recv(s2, {"cmd": "close"})
    s2.close()

    print("\n" + ("PASS" if passed else "FAIL"))

if __name__ == "__main__":
    main()