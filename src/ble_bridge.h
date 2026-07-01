#pragma once
#include <stdint.h>
#include <stddef.h>

// Nordic UART Service bridge (NimBLE). Same surface as upstream's
// ble_bridge.h so the rest of the port maps 1:1.
//
// Service UUID  6e400001-b5a3-f393-e0a9-e50e24dcca9e
// RX char       6e400002-...   (desktop -> device, WRITE/WRITE_NR, encrypted)
// TX char       6e400003-...   (device -> desktop, NOTIFY, encrypted)
//
// Security: LE Secure Connections + MITM + bonding, DisplayOnly IO — the
// device shows a 6-digit passkey (blePasskey()) that the user types on the
// desktop. Reconnects reuse the stored LTK.

void bleInit(const char* deviceName);
// Drain both transports (USB Serial + BLE), assemble newline-delimited JSON,
// apply snapshots to tama and dispatch cmd/ack traffic (upstream dataPoll).
void bridgePoll();
// One JSON line + '\n' to BOTH transports (upstream sendCmd/_xAck behavior:
// acks and permission decisions go everywhere; a clientless side just drops).
void bridgeSendLine(const char* json);

// Folder-push transfer status (for the install-progress screen).
bool xferActive();
uint32_t xferProgress();
uint32_t xferTotal();

bool bleConnected();       // a central is connected (regardless of pairing)
bool bleSecure();          // current link is encrypted+authenticated
// Non-zero while a pairing passkey should be on screen; cleared on auth
// complete or disconnect.
uint32_t blePasskey();
void bleClearBonds();      // "unpair" cmd + factory reset
size_t bleAvailable();
int bleRead();
size_t bleWrite(const uint8_t* data, size_t len);
