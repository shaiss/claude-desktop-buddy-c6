#!/usr/bin/env python3
"""M7 acceptance test: approve/deny via the debug input seam, pet cycling,
demo toggle, and stats accounting.

Usage: python tools/test_input.py [COM13]
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
s.port = find_port(); s.baudrate = 115200; s.timeout = 0.2
s.dtr = False; s.rts = False
s.open()

results = []


def send_raw(txt):
    s.write((txt + "\n").encode())


def send(obj):
    send_raw(json.dumps(obj))


def collect(secs):
    end = time.time() + secs
    out = []
    while time.time() < end:
        l = s.readline().decode(errors="replace").strip()
        if l:
            out.append(l)
    return out


def check(name, ok, detail=""):
    results.append((name, ok))
    print(f"{'PASS' if ok else 'FAIL'}: {name}" + (f" — {detail}" if detail else ""))


def expect(name, lines, needle):
    ok = any(needle in l for l in lines)
    check(name, ok, "" if ok else f"missing: {needle}  got: {lines[-6:]}")
    return ok


collect(1)

# baseline stats
send({"cmd": "status"})
lines = collect(2)
appr0 = deny0 = None
for l in lines:
    if '"ack":"status"' in l:
        st = json.loads(l)["data"]["stats"]
        appr0, deny0 = st["appr"], st["deny"]
print(f"baseline appr={appr0} deny={deny0}")

# 1. prompt -> !short approves: permission cmd emitted, heart one-shot, sent latch
send({"total": 2, "running": 1, "waiting": 1, "msg": "approve: Bash",
      "prompt": {"id": "req_in1", "tool": "Bash", "hint": "git push"}})
collect(1.5)
send_raw("!short")
lines = collect(2.5)
expect("approve emits permission once", lines,
       '{"cmd":"permission","id":"req_in1","decision":"once"}')
expect("approve logged with timing", lines, "[input] approved req_in1")
expect("fast approve -> heart one-shot", lines, "-> heart (one-shot")

# 2. second !short is swallowed (responseSent latch)
send_raw("!short")
lines = collect(2)
check("second press swallowed (latch)",
      not any('"cmd":"permission"' in l for l in lines),
      "" if not any('"cmd":"permission"' in l for l in lines) else "resent!")
# ... but it should have cycled the pet instead (no active prompt anymore)
expect("latched press falls through to next-pet", lines, "[input] pet ->")

# 3. new prompt -> !long denies
send({"total": 2, "running": 1, "waiting": 1, "msg": "approve: Write",
      "prompt": {"id": "req_in2", "tool": "Write", "hint": "main.cpp"}})
collect(1.5)
send_raw("!long")
lines = collect(2.5)
expect("deny emits permission deny", lines,
       '{"cmd":"permission","id":"req_in2","decision":"deny"}')

# clear prompt
send({"total": 1, "running": 0, "waiting": 0, "msg": "idle"})
collect(1)

# 4. stats reflect one approval + one denial, velocity recorded
send({"cmd": "status"})
lines = collect(2)
ok = False
detail = "no status ack"
for l in lines:
    if '"ack":"status"' in l:
        st = json.loads(l)["data"]["stats"]
        ok = st["appr"] == appr0 + 1 and st["deny"] == deny0 + 1
        detail = f"appr {appr0}->{st['appr']} deny {deny0}->{st['deny']} vel={st['vel']}"
        break
check("stats counted approval+denial", ok, detail)

# 5. !short with no prompt cycles pets
send_raw("!short")
lines = collect(1.5)
expect("short press cycles pet", lines, "[input] pet ->")

# 6. !long with no prompt toggles demo on; !long again off (exit via serial works)
send_raw("!long")
lines = collect(2)
expect("long press -> demo on", lines, "[input] demo on")
send_raw("!long")
lines = collect(2)
expect("long press -> demo off", lines, "[input] demo off")

print()
fails = [n for n, ok in results if not ok]
print(f"{len(results) - len(fails)}/{len(results)} checks passed")
if fails:
    print("failed:", ", ".join(fails))
s.close()
sys.exit(1 if fails else 0)
