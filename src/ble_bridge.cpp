#include "ble_bridge.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_random.h>

#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// Incoming bytes buffered in a ring for bleRead()/bleAvailable(), same
// sizing as upstream (holds a transcript snapshot line plus headroom).
static const size_t RX_CAP = 2048;
static uint8_t          rxBuf[RX_CAP];
static volatile size_t  rxHead = 0;
static volatile size_t  rxTail = 0;

static NimBLEServer*         server = nullptr;
static NimBLECharacteristic* txChar = nullptr;
static volatile bool         connected = false;
static volatile bool         secure = false;
static volatile uint32_t     passkey = 0;
static volatile uint16_t     mtu = 23;

static void rxPush(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    size_t next = (rxHead + 1) % RX_CAP;
    if (next == rxTail) return;  // full — drop; GATT flow control should prevent this
    rxBuf[rxHead] = p[i];
    rxHead = next;
  }
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& connInfo) override {
    NimBLEAttValue v = c->getValue();
    if (v.size()) rxPush(v.data(), v.size());
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, NimBLEConnInfo& connInfo) override {
    connected = true;
    Serial.println("[ble] connected");
  }
  void onDisconnect(NimBLEServer* s, NimBLEConnInfo& connInfo, int reason) override {
    connected = false;
    secure = false;
    passkey = 0;
    mtu = 23;
    Serial.printf("[ble] disconnected (reason %d)\n", reason);
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t newMtu, NimBLEConnInfo& connInfo) override {
    mtu = newMtu;
    Serial.printf("[ble] mtu=%u\n", newMtu);
  }
  // DisplayOnly pairing: NimBLE asks us for the passkey to show (Bluedroid
  // instead notified us of a stack-chosen one — same UX, inverted API).
  uint32_t onPassKeyDisplay() override {
    uint32_t pk = 100000 + (esp_random() % 900000);
    passkey = pk;
    Serial.printf("[ble] passkey %06lu\n", (unsigned long)pk);
    return pk;
  }
  void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
    passkey = 0;
    secure = connInfo.isEncrypted() && connInfo.isAuthenticated();
    Serial.printf("[ble] auth %s\n", secure ? "ok" : "FAIL");
    if (!secure && server) server->disconnect(connInfo.getConnHandle());
  }
};

void bleInit(const char* deviceName) {
  NimBLEDevice::init(deviceName);
  // Ask for the biggest MTU; macOS typically negotiates ~185.
  NimBLEDevice::setMTU(517);

  NimBLEDevice::setSecurityAuth(true /*bond*/, true /*mitm*/, true /*sc*/);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService* svc = server->createService(NUS_SERVICE_UUID);

  // Encrypted-only characteristics: first GATT access triggers OS pairing.
  txChar = svc->createCharacteristic(
    NUS_TX_UUID,
    NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::READ_AUTHEN);

  NimBLECharacteristic* rxChar = svc->createCharacteristic(
    NUS_RX_UUID,
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
    NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN);
  rxChar->setCallbacks(new RxCallbacks());

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setName(deviceName);
  adv->addServiceUUID(svc->getUUID());
  adv->enableScanResponse(true);
  adv->setPreferredParams(0x06, 0x12);   // iOS/macOS-friendly connection interval
  NimBLEDevice::startAdvertising();
  Serial.printf("[ble] advertising as '%s'\n", deviceName);
}

bool bleConnected()   { return connected; }
bool bleSecure()      { return secure; }
uint32_t blePasskey() { return passkey; }

void bleClearBonds() {
  int n = NimBLEDevice::getNumBonds();
  NimBLEDevice::deleteAllBonds();
  Serial.printf("[ble] cleared %d bond(s)\n", n);
}

size_t bleAvailable() {
  return (rxHead + RX_CAP - rxTail) % RX_CAP;
}

int bleRead() {
  if (rxHead == rxTail) return -1;
  int b = rxBuf[rxTail];
  rxTail = (rxTail + 1) % RX_CAP;
  return b;
}

size_t bleWrite(const uint8_t* data, size_t len) {
  if (!connected || !txChar) return 0;
  // Notify payload caps at MTU-3; keep upstream's 180-byte ceiling.
  size_t chunk = mtu > 3 ? (size_t)(mtu - 3) : 20;
  if (chunk > 180) chunk = 180;
  size_t sent = 0;
  while (sent < len) {
    size_t n = len - sent;
    if (n > chunk) n = chunk;
    txChar->setValue(data + sent, n);
    txChar->notify();
    sent += n;
    delay(4);   // let the stack flush before the next chunk
  }
  return sent;
}
