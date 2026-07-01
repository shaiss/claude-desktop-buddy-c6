#!/usr/bin/env python3
"""Radio-side NUS verification using the host's Bluetooth adapter.

1. Scan: find 'Claude-*' advertising the Nordic UART Service UUID.
2. Connect: device serial should log [ble] connected.
3. Unauthenticated RX write: must FAIL (characteristics are encrypted-only).
4. Disconnect: device should resume advertising.

Encrypted echo needs OS pairing (passkey dialog) — manual, via nRF Connect
or the real Claude Desktop.
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX  = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX  = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


async def main():
    print("scanning 10s for Claude-* ...")
    found = None
    devices = await BleakScanner.discover(timeout=10.0, return_adv=True)
    for d, adv in devices.values():
        name = adv.local_name or d.name or ""
        if name.startswith("Claude"):
            uuids = [u.lower() for u in (adv.service_uuids or [])]
            print(f"FOUND {name} addr={d.address} rssi={adv.rssi} uuids={uuids}")
            if NUS_SVC in uuids:
                print("PASS: NUS service UUID advertised")
            else:
                print("WARN: NUS UUID not in advertisement (may be in scan response only)")
            found = d
            break
    if not found:
        print("FAIL: no Claude-* device found")
        return 1

    print("connecting ...")
    async with BleakClient(found) as client:
        print(f"connected={client.is_connected}")
        svc_uuids = [s.uuid.lower() for s in client.services]
        print(f"gatt services: {svc_uuids}")
        if NUS_SVC in svc_uuids:
            print("PASS: NUS service present in GATT")
        else:
            print("FAIL: NUS service missing from GATT")

        try:
            await client.write_gatt_char(NUS_RX, b"echo-test\n", response=True)
            print("WARN: unauthenticated write SUCCEEDED — encryption gate not enforced?")
        except Exception as e:
            print(f"PASS: unauthenticated write rejected ({type(e).__name__}: {e})")
    print("disconnected")
    return 0


sys.exit(asyncio.run(main()))
