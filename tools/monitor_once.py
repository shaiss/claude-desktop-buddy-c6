#!/usr/bin/env python3
"""Capture N seconds of serial output and exit. Non-interactive replacement
for `pio device monitor` so automated verification can read the log.

Usage: python tools/monitor_once.py [COM12 [seconds]]
DTR/RTS are held low so opening the port doesn't reset the chip.
"""
import sys, time

import serial
from serial.tools import list_ports


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    for p in list_ports.comports():
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    sys.exit("no ESP32-C6 found (pass COM port explicitly)")


secs = float(sys.argv[2]) if len(sys.argv) > 2 else 6.0
s = serial.Serial()
s.port = find_port()
s.baudrate = 115200
s.timeout = 0.3
s.dtr = False
s.rts = False
s.open()

end = time.time() + secs
while time.time() < end:
    line = s.readline().decode("utf-8", errors="replace").rstrip()
    if line:
        print(line, flush=True)
s.close()
