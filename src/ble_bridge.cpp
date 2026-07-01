#include "ble_bridge.h"
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_random.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <mbedtls/base64.h>
#include <sys/time.h>
#include "state.h"
#include "stats.h"
#include "characters/ascii_pets.h"
#include "characters/gif_player.h"

// GIF-vs-ASCII mode flags live in main.cpp (upstream pattern).
extern bool buddyMode, gifAvailable;

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

// ============================================================================
// Wire protocol (upstream data.h + xfer.h). Newline-delimited UTF-8 JSON on
// both USB Serial and BLE NUS, one object per line.
// ============================================================================

void bridgeSendLine(const char* json) {
  size_t n = strlen(json);
  Serial.write((const uint8_t*)json, n);
  Serial.write('\n');
  bleWrite((const uint8_t*)json, n);
  bleWrite((const uint8_t*)"\n", 1);
}

static void _ack(const char* what, bool ok, uint32_t n = 0) {
  char b[64];
  snprintf(b, sizeof(b), "{\"ack\":\"%s\",\"ok\":%s,\"n\":%lu}",
           what, ok ? "true" : "false", (unsigned long)n);
  bridgeSendLine(b);
}

// --- folder push receiver ---------------------------------------------------

static File     _xFile;
static uint32_t _xExpected = 0, _xWritten = 0;
static char     _xCharName[24] = "";
static bool     _xActive = false;
static uint32_t _xTotal = 0, _xTotalWritten = 0;

bool xferActive()       { return _xActive; }
uint32_t xferProgress() { return _xTotalWritten; }
uint32_t xferTotal()    { return _xTotal; }

static uint32_t _xWipeDir(const char* dir) {
  File d = LittleFS.open(dir);
  if (!d || !d.isDirectory()) { LittleFS.mkdir(dir); return 0; }
  uint32_t freed = 0;
  File f = d.openNextFile();
  while (f) {
    freed += f.size();
    char p[80];
    snprintf(p, sizeof(p), "%s/%s", dir, f.name());
    f.close();
    LittleFS.remove(p);
    f = d.openNextFile();
  }
  d.close();
  return freed;
}

// One character on the device at a time (upstream policy): wipe everything
// under /characters/ before an install so stale packs don't eat the FS.
static uint32_t _xWipeAllChars() {
  File root = LittleFS.open("/characters");
  if (!root || !root.isDirectory()) { LittleFS.mkdir("/characters"); return 0; }
  uint32_t freed = 0;
  File sub = root.openNextFile();
  while (sub) {
    if (sub.isDirectory()) {
      char p[64];
      snprintf(p, sizeof(p), "/characters/%s", sub.name());
      sub.close();
      freed += _xWipeDir(p);
      LittleFS.rmdir(p);
    } else {
      sub.close();
    }
    sub = root.openNextFile();
  }
  root.close();
  return freed;
}

// REFERENCE.md: validate pushed paths — reject traversal and absolute paths.
// (Upstream skips this; the protocol doc explicitly tells implementers to do it.)
static bool _xPathOk(const char* p) {
  if (!p || !*p) return false;
  if (p[0] == '/' || p[0] == '\\') return false;
  if (strstr(p, "..")) return false;
  if (strchr(p, ':')) return false;
  return true;
}

// Handles any incoming JSON with a "cmd" key. Returns true if consumed
// (caller skips snapshot parsing). Mirrors upstream xferCommand().
static bool xferCommand(JsonDocument& doc) {
  const char* cmd = doc["cmd"];
  if (!cmd) return false;

  if (strcmp(cmd, "name") == 0) {
    const char* n = doc["name"];
    if (n) petNameSet(n);
    _ack("name", n != nullptr);
    return true;
  }

  if (strcmp(cmd, "species") == 0) {
    uint8_t idx = doc["idx"] | 0xFF;
    speciesIdxSave(idx);
    buddyMode = !(gifAvailable && idx == 0xFF);
    if (buddyMode) buddySetSpeciesIdx(idx);
    _ack("species", true);
    return true;
  }

  if (strcmp(cmd, "unpair") == 0) {
    bleClearBonds();
    _ack("unpair", true);
    return true;
  }

  if (strcmp(cmd, "owner") == 0) {
    const char* n = doc["name"];
    if (n) ownerSet(n);
    _ack("owner", n != nullptr);
    return true;
  }

  if (strcmp(cmd, "status") == 0) {
    // Manual printf, fixed shape (upstream choice: less heap churn than
    // serializing). No "bat" object — this board has no battery telemetry;
    // REFERENCE.md allows omitting fields you don't have.
    char b[320];
    snprintf(b, sizeof(b),
      "{\"ack\":\"status\",\"ok\":true,\"n\":0,\"data\":{"
      "\"name\":\"%s\",\"owner\":\"%s\",\"sec\":%s,"
      "\"sys\":{\"up\":%lu,\"heap\":%u,\"fsFree\":%lu,\"fsTotal\":%lu},"
      "\"stats\":{\"appr\":%u,\"deny\":%u,\"vel\":%u,\"nap\":%lu,\"lvl\":%u}"
      "}}",
      petName(), ownerName(), bleSecure() ? "true" : "false",
      (unsigned long)(millis() / 1000), (unsigned)ESP.getFreeHeap(),
      (unsigned long)(LittleFS.totalBytes() - LittleFS.usedBytes()),
      (unsigned long)LittleFS.totalBytes(),
      stats().approvals, stats().denials, statsMedianVelocity(),
      (unsigned long)stats().napSeconds, stats().level);
    bridgeSendLine(b);
    return true;
  }

  if (strcmp(cmd, "char_begin") == 0) {
    const char* name = doc["name"] | "pet";
    _xTotal = doc["total"] | 0;

    // Fit check before touching the filesystem, so a failure leaves the
    // current character intact. Headroom for LittleFS metadata.
    uint32_t free_ = LittleFS.totalBytes() - LittleFS.usedBytes();
    uint32_t reclaimable = 0;
    {
      File r = LittleFS.open("/characters");
      if (r && r.isDirectory()) {
        File s = r.openNextFile();
        while (s) {
          if (s.isDirectory()) {
            File f = s.openNextFile();
            while (f) { reclaimable += f.size(); f.close(); f = s.openNextFile(); }
          }
          s.close(); s = r.openNextFile();
        }
        r.close();
      }
    }
    uint32_t available = free_ + reclaimable;
    if (_xTotal > 0 && _xTotal + 4096 > available) {
      char b[96];
      snprintf(b, sizeof(b),
        "{\"ack\":\"char_begin\",\"ok\":false,\"n\":%lu,\"error\":\"need %luK, have %luK\"}",
        (unsigned long)available, (unsigned long)(_xTotal / 1024),
        (unsigned long)(available / 1024));
      bridgeSendLine(b);
      return true;
    }

    strncpy(_xCharName, name, sizeof(_xCharName) - 1);
    _xCharName[sizeof(_xCharName) - 1] = 0;
    if (!_xPathOk(_xCharName)) { _ack("char_begin", false); return true; }
    characterClose();
    _xWipeAllChars();
    char dir[48];
    snprintf(dir, sizeof(dir), "/characters/%s", _xCharName);
    LittleFS.mkdir(dir);
    _xTotalWritten = 0;
    _xActive = true;
    _ack("char_begin", true);
    return true;
  }

  // Not mid-transfer: swallow stray transfer cmds (desktop will time out),
  // but let "permission" fall through — that's device->desktop traffic, not ours.
  if (!_xActive) return strcmp(cmd, "permission") != 0;

  if (strcmp(cmd, "file") == 0) {
    const char* path = doc["path"];
    _xExpected = doc["size"] | 0;
    _xWritten = 0;
    if (!_xPathOk(path)) { _ack("file", false); return true; }
    char full[80];
    snprintf(full, sizeof(full), "/characters/%s/%s", _xCharName, path);
    _xFile = LittleFS.open(full, "w");
    _ack("file", (bool)_xFile);
    return true;
  }

  if (strcmp(cmd, "chunk") == 0) {
    const char* b64 = doc["d"];
    if (!b64 || !_xFile) { _ack("chunk", false); return true; }
    uint8_t buf[300];
    size_t outLen = 0;
    int rc = mbedtls_base64_decode(buf, sizeof(buf), &outLen,
                                   (const uint8_t*)b64, strlen(b64));
    if (rc != 0) { _ack("chunk", false); return true; }
    _xFile.write(buf, outLen);
    _xWritten += outLen;
    _xTotalWritten += outLen;
    // Ack every chunk — LittleFS writes block on flash erase and the sender
    // waits for each ack, which is the flow control.
    _ack("chunk", true, _xWritten);
    return true;
  }

  if (strcmp(cmd, "file_end") == 0) {
    bool ok = _xFile && (_xWritten == _xExpected || _xExpected == 0);
    if (_xFile) _xFile.close();
    _ack("file_end", ok, _xWritten);
    return true;
  }

  if (strcmp(cmd, "char_end") == 0) {
    _xActive = false;
    bool ok = characterInit(_xCharName);
    if (ok) { buddyMode = false; gifAvailable = true; speciesIdxSave(0xFF); }
    _ack("char_end", ok);
    return true;
  }

  return false;
}

// --- JSON application (upstream _applyJson) -----------------------------------

static void _applyJson(const char* line, TamaState* out) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;
  if (xferCommand(doc)) { dataMarkLive(); return; }

  // {"time":[epoch_sec, tz_offset_sec]} — store LOCAL time as system time
  // (upstream set the RTC to local components; same trick, no RTC chip).
  JsonArray t = doc["time"];
  if (!t.isNull() && t.size() == 2) {
    struct timeval tv = { (time_t)t[0].as<uint32_t>() + (int32_t)t[1], 0 };
    settimeofday(&tv, nullptr);
    dataMarkRtcValid();
    dataMarkLive();
    return;
  }

  out->sessionsTotal     = doc["total"]     | out->sessionsTotal;
  out->sessionsRunning   = doc["running"]   | out->sessionsRunning;
  out->sessionsWaiting   = doc["waiting"]   | out->sessionsWaiting;
  out->recentlyCompleted = doc["completed"] | false;
  uint32_t bridgeTokens = doc["tokens"] | 0;
  if (doc["tokens"].is<uint32_t>()) statsOnBridgeTokens(bridgeTokens);
  out->tokensToday = doc["tokens_today"] | out->tokensToday;
  const char* m = doc["msg"];
  if (m) { strncpy(out->msg, m, sizeof(out->msg) - 1); out->msg[sizeof(out->msg) - 1] = 0; }
  JsonArray la = doc["entries"];
  if (!la.isNull()) {
    uint8_t n = 0;
    for (JsonVariant v : la) {
      if (n >= 8) break;
      const char* s = v.as<const char*>();
      strncpy(out->lines[n], s ? s : "", 91);
      out->lines[n][91] = 0;
      n++;
    }
    if (n != out->nLines || (n > 0 && strcmp(out->lines[n - 1], out->msg) != 0)) {
      out->lineGen++;
    }
    out->nLines = n;
  }
  JsonObject pr = doc["prompt"];
  if (!pr.isNull()) {
    const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
    strncpy(out->promptId,   pid ? pid : "", sizeof(out->promptId) - 1);   out->promptId[sizeof(out->promptId) - 1] = 0;
    strncpy(out->promptTool, pt  ? pt  : "", sizeof(out->promptTool) - 1); out->promptTool[sizeof(out->promptTool) - 1] = 0;
    strncpy(out->promptHint, ph  ? ph  : "", sizeof(out->promptHint) - 1); out->promptHint[sizeof(out->promptHint) - 1] = 0;
  } else {
    // Any snapshot without a prompt clears the pending one (upstream semantics).
    out->promptId[0] = 0; out->promptTool[0] = 0; out->promptHint[0] = 0;
  }
  out->lastUpdated = millis();
  dataMarkLive();
}

// --- line assembly + poll ------------------------------------------------------

template <size_t N>
struct LineBuf {
  char buf[N];
  uint16_t len = 0;
  void push(char c, TamaState* out) {
    if (c == '\n' || c == '\r') {
      if (len > 0) {
        buf[len] = 0;
        if (buf[0] == '{') _applyJson(buf, out);
        len = 0;
      }
    } else if (len < N - 1) {
      buf[len++] = c;
    }
  }
};

static LineBuf<1024> _usbLine, _btLine;

void bridgePoll() {
  if (dataDemo()) { dataDemoTick(); return; }

  while (Serial.available()) _usbLine.push((char)Serial.read(), &tama);
  while (bleAvailable()) {
    int c = bleRead();
    if (c < 0) break;
    _btLine.push((char)c, &tama);
  }

  tama.connected = dataConnected();
  if (!tama.connected) {
    tama.sessionsTotal = 0; tama.sessionsRunning = 0; tama.sessionsWaiting = 0;
    tama.recentlyCompleted = false; tama.lastUpdated = millis();
    strncpy(tama.msg, "No Claude connected", sizeof(tama.msg) - 1);
    tama.msg[sizeof(tama.msg) - 1] = 0;
  }
}
