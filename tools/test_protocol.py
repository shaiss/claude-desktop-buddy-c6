#!/usr/bin/env python3
"""M4 acceptance test: drive the wire protocol over USB serial and verify
state transitions, acks, level-up, prompt handling, and BLE-driven sleep.

Usage: python tools/test_protocol.py [COM13]
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
    sys.exit("no ESP32-C6 found")


s = serial.Serial()
s.port = find_port()
s.baudrate = 115200
s.timeout = 0.2
s.dtr = False
s.rts = False
s.open()

results = []
log = []


def send(obj):
    s.write((json.dumps(obj) + "\n").encode())


def collect(seconds):
    end = time.time() + seconds
    out = []
    while time.time() < end:
        line = s.readline().decode("utf-8", errors="replace").strip()
        if line:
            out.append(line)
            log.append(line)
    return out


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{'PASS' if ok else 'FAIL'}: {name}" + (f" — {detail}" if detail else ""))


def expect(name, lines, needle):
    hit = any(needle in l for l in lines)
    check(name, hit, needle if not hit else "")
    return hit


print(f"port: {s.port}\n")
collect(1)  # flush

# 1. heartbeat -> idle (from sleep or idle; device may already be idle)
send({"total": 1, "running": 1, "waiting": 0, "msg": "one session",
      "entries": ["10:01 reading file...", "10:02 running tests"]})
lines = collect(2)

# 2. busy needs running >= 3
send({"total": 4, "running": 3, "waiting": 0, "msg": "3 running"})
lines = collect(2)
expect("busy on running>=3", lines, "-> busy")

# 3. attention + prompt
send({"total": 2, "running": 1, "waiting": 1, "msg": "approve: Bash",
      "prompt": {"id": "req_test1", "tool": "Bash", "hint": "rm -rf /tmp/foo"}})
lines = collect(2)
expect("attention on waiting>0", lines, "-> attention")
expect("prompt arrival logged", lines, "[prompt] id=req_test1 tool=Bash")

# 4. prompt cleared by snapshot without prompt; completed -> celebrate
send({"total": 1, "running": 0, "waiting": 0, "completed": True, "msg": "done"})
lines = collect(2)
expect("celebrate on completed", lines, "-> celebrate")

# 5. back to idle
send({"total": 1, "running": 0, "waiting": 0, "msg": "idle again"})
lines = collect(2)
expect("idle after celebrate", lines, "-> idle")

# 6. status cmd -> ack with our data shape
send({"cmd": "status"})
lines = collect(2)
ok = False
detail = "no status ack seen"
for l in lines:
    if '"ack":"status"' in l:
        try:
            a = json.loads(l)
            d = a["data"]
            ok = (a["ok"] is True and "sys" in d and "stats" in d
                  and "fsTotal" in d["sys"] and "appr" in d["stats"]
                  and "bat" not in d)
            detail = l[:120]
        except Exception as e:
            detail = f"parse: {e}"
        break
check("status ack shape", ok, detail)

# 7. owner cmd -> ack, then persists
send({"cmd": "owner", "name": "Shai"})
lines = collect(2)
expect("owner ack", lines, '"ack":"owner","ok":true')

# 8. name cmd
send({"cmd": "name", "name": "Clawd"})
lines = collect(2)
expect("name ack", lines, '"ack":"name","ok":true')

# 9. time sync (no ack, must not disturb)
send({"time": [int(time.time()), -25200]})
collect(1)

# 10. species cmd
send({"cmd": "species", "idx": 4})   # cat
lines = collect(2)
expect("species ack", lines, '"ack":"species","ok":true')

# 11. token level-up -> celebrate one-shot
send({"total": 1, "running": 0, "waiting": 0, "tokens": 1000})
collect(1)
send({"total": 1, "running": 0, "waiting": 0, "tokens": 61000})
lines = collect(3)
expect("level-up celebrate", lines, "-> celebrate")

# 12. unknown cmd swallowed silently (no ack, no crash)
send({"cmd": "bogus_cmd_xyz"})
lines = collect(2)
check("unknown cmd swallowed", not any('"ack":"bogus' in l for l in lines))

# 13. silence -> sleep (30s data window + 30s sleep threshold)
print("\nwaiting ~70s of silence for BLE-driven sleep...")
lines = collect(70)
expect("sleep after silence", lines, "-> sleep")

# 14. wake on data
send({"total": 1, "running": 0, "waiting": 0, "msg": "back"})
lines = collect(2)
expect("wake on data", lines, "-> idle")

print()
fails = [n for n, ok in results if not ok]
print(f"{len(results) - len(fails)}/{len(results)} checks passed")
if fails:
    print("failed:", ", ".join(fails))
    print("\n--- full log tail ---")
    for l in log[-40:]:
        print(" ", l)
s.close()
sys.exit(1 if fails else 0)
