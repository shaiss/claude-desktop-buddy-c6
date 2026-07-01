#!/usr/bin/env python3
"""Prove the buddy reacts to serial JSON. Cycles states every 3s.

Usage: python tools/test_serial.py [COM12]
Port defaults to auto-detect of the ESP32-C6 USB Serial/JTAG (VID:PID 303A:1001).
Adapted from upstream tools/test_serial.py (macOS glob -> argv/VID autodetect).
"""
import json, sys, time

import serial
from serial.tools import list_ports


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in list_ports.comports():
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    sys.exit("no ESP32-C6 found (pass COM port explicitly)")


port = find_port()
s = serial.Serial(port, 115200, timeout=0.2)
print(f"writing to {port} — watch the board\n")

states = [
    {"total": 0, "running": 0, "waiting": 0},  # connected, nothing open -> idle
    {"total": 2, "running": 1, "waiting": 0},  # -> idle
    {"total": 4, "running": 3, "waiting": 0},  # -> busy (needs running >= 3)
    {"total": 2, "running": 1, "waiting": 1},  # -> attention, LED blinks
]
for i in range(20):
    st = states[i % len(states)]
    s.write((json.dumps(st) + "\n").encode())
    print(f"  -> {st}")
    deadline = time.time() + 3
    while time.time() < deadline:
        line = s.readline().decode("utf-8", errors="replace").strip()
        if line:
            print(f"     <- {line}")
