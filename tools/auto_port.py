# PlatformIO extra_script: resolve upload/monitor port by USB VID:PID at build
# time. The C6's native USB Serial/JTAG has flapped between COM12 and COM13,
# so a hardcoded port goes stale.
Import("env")


def _find():
    try:
        from serial.tools import list_ports
    except ImportError:
        return None
    for p in list_ports.comports():
        if p.vid == 0x303A and p.pid == 0x1001:
            return p.device
    return None


_port = _find()
if _port:
    env.Replace(UPLOAD_PORT=_port, MONITOR_PORT=_port)
    print(f"auto_port: ESP32-C6 at {_port}")
else:
    print("auto_port: no ESP32-C6 (303A:1001) present — upload will fail until replugged")
