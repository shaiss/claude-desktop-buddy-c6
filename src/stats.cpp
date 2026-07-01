#include "stats.h"
#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

static Stats _stats;
static Preferences _prefs;
static bool _dirty = false;

void statsLoad() {
  _prefs.begin("buddy", true);
  _stats.napSeconds = _prefs.getUInt("nap", 0);
  _stats.approvals  = _prefs.getUShort("appr", 0);
  _stats.denials    = _prefs.getUShort("deny", 0);
  _stats.velIdx     = _prefs.getUChar("vidx", 0);
  _stats.velCount   = _prefs.getUChar("vcnt", 0);
  _stats.level      = _prefs.getUChar("lvl", 0);
  _stats.tokens     = _prefs.getUInt("tok", 0);
  size_t got = _prefs.getBytes("vel", _stats.velocity, sizeof(_stats.velocity));
  if (got != sizeof(_stats.velocity)) memset(_stats.velocity, 0, sizeof(_stats.velocity));
  _prefs.end();
  // Level derives from tokens; backfill if NVS predates token tracking.
  if (_stats.tokens == 0 && _stats.level > 0) {
    _stats.tokens = (uint32_t)_stats.level * TOKENS_PER_LEVEL;
  }
}

void statsSave() {
  if (!_dirty) return;
  _prefs.begin("buddy", false);
  _prefs.putUInt("nap", _stats.napSeconds);
  _prefs.putUShort("appr", _stats.approvals);
  _prefs.putUShort("deny", _stats.denials);
  _prefs.putUChar("vidx", _stats.velIdx);
  _prefs.putUChar("vcnt", _stats.velCount);
  _prefs.putUChar("lvl", _stats.level);
  _prefs.putUInt("tok", _stats.tokens);
  _prefs.putBytes("vel", _stats.velocity, sizeof(_stats.velocity));
  _prefs.end();
  _dirty = false;
}

void statsOnApproval(uint32_t secondsToRespond) {
  _stats.approvals++;
  _stats.velocity[_stats.velIdx] =
      (uint16_t)(secondsToRespond > 65535u ? 65535u : secondsToRespond);
  _stats.velIdx = (_stats.velIdx + 1) % 8;
  if (_stats.velCount < 8) _stats.velCount++;
  _dirty = true; statsSave();
}

// Bridge sends its cumulative total since IT started; we track deltas.
// First-sight latch so a device reboot doesn't re-credit the whole session;
// a drop means the bridge restarted — resync without crediting.
static uint32_t _lastBridgeTokens = 0;
static bool _tokensSynced = false;
static bool _levelUpPending = false;

void statsOnBridgeTokens(uint32_t bridgeTotal) {
  if (!_tokensSynced) {
    _lastBridgeTokens = bridgeTotal;
    _tokensSynced = true;
    return;
  }
  if (bridgeTotal < _lastBridgeTokens) {
    _lastBridgeTokens = bridgeTotal;
    return;
  }
  uint32_t delta = bridgeTotal - _lastBridgeTokens;
  _lastBridgeTokens = bridgeTotal;
  if (delta == 0) return;

  uint8_t lvlBefore = (uint8_t)(_stats.tokens / TOKENS_PER_LEVEL);
  _stats.tokens += delta;
  uint8_t lvlAfter = (uint8_t)(_stats.tokens / TOKENS_PER_LEVEL);

  // Heartbeats are timer-driven telemetry — persist only on the level-up
  // milestone, not every delta (NVS wear). Worst case on hard power-off:
  // lose up to 50K tokens of progress.
  if (lvlAfter > lvlBefore) {
    _stats.level = lvlAfter;
    _levelUpPending = true;
    _dirty = true; statsSave();
  }
}

bool statsPollLevelUp() {
  bool r = _levelUpPending;
  _levelUpPending = false;
  return r;
}

void statsOnDenial() { _stats.denials++; _dirty = true; statsSave(); }

void statsOnNapEnd(uint32_t seconds) {
  _stats.napSeconds += seconds;
  _dirty = true; statsSave();
}

uint16_t statsMedianVelocity() {
  if (_stats.velCount == 0) return 0;
  uint16_t tmp[8];
  memcpy(tmp, _stats.velocity, sizeof(tmp));
  uint8_t n = _stats.velCount;
  for (uint8_t i = 1; i < n; i++) {          // insertion sort, n <= 8
    uint16_t k = tmp[i]; int8_t j = i - 1;
    while (j >= 0 && tmp[j] > k) { tmp[j + 1] = tmp[j]; j--; }
    tmp[j + 1] = k;
  }
  return tmp[n / 2];
}

// 0..4 tier. Velocity sets the base; heavy denial ratio drags it down.
uint8_t statsMoodTier() {
  uint16_t vel = statsMedianVelocity();
  int8_t tier;
  if (vel == 0) tier = 2;              // no data: neutral
  else if (vel < 15) tier = 4;
  else if (vel < 30) tier = 3;
  else if (vel < 60) tier = 2;
  else if (vel < 120) tier = 1;
  else tier = 0;
  uint16_t a = _stats.approvals, d = _stats.denials;
  if (a + d >= 3) {                    // need a few decisions before judging
    if (d > a) tier -= 2;
    else if (d * 2 > a) tier -= 1;     // deny rate > 33%
  }
  if (tier < 0) tier = 0;
  return (uint8_t)tier;
}

// Energy: 3/5 at boot, refills on nap end, drains 1 tier per 2h awake.
static uint32_t _lastNapEndMs = 0;
static uint8_t  _energyAtNap  = 3;

void statsOnWake() { _lastNapEndMs = millis(); _energyAtNap = 5; }

uint8_t statsEnergyTier() {
  uint32_t hoursSince = (millis() - _lastNapEndMs) / 3600000;
  int8_t e = (int8_t)_energyAtNap - (int8_t)(hoursSince / 2);
  if (e < 0) e = 0;
  if (e > 5) e = 5;
  return (uint8_t)e;
}

uint8_t statsFedProgress() {
  return (uint8_t)((_stats.tokens % TOKENS_PER_LEVEL) / (TOKENS_PER_LEVEL / 10));
}

// --- Settings ---------------------------------------------------------------

static Settings _settings = { true, true };

void settingsLoad() {
  _prefs.begin("buddy", true);
  _settings.led = _prefs.getBool("s_led", true);
  _settings.hud = _prefs.getBool("s_hud", true);
  _prefs.end();
}

void settingsSave() {
  _prefs.begin("buddy", false);
  _prefs.putBool("s_led", _settings.led);
  _prefs.putBool("s_hud", _settings.hud);
  _prefs.end();
}

Settings& settings() { return _settings; }
const Stats& stats() { return _stats; }

// --- Names ------------------------------------------------------------------

static char _petName[24] = "Buddy";
static char _ownerName[32] = "";

void petNameLoad() {
  _prefs.begin("buddy", true);
  _prefs.getString("petname", _petName, sizeof(_petName));
  _prefs.getString("owner", _ownerName, sizeof(_ownerName));
  _prefs.end();
}

// Strip JSON-breaking chars — names get printf'd into the status ack
// unescaped. A stored quote would break that endpoint until re-set.
static void _safeCopy(char* dst, size_t dstLen, const char* src) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j < dstLen - 1; i++) {
    char c = src[i];
    if (c != '"' && c != '\\' && c >= 0x20) dst[j++] = c;
  }
  dst[j] = 0;
}

void petNameSet(const char* name) {
  _safeCopy(_petName, sizeof(_petName), name);
  _prefs.begin("buddy", false);
  _prefs.putString("petname", _petName);
  _prefs.end();
}

const char* petName() { return _petName; }

void ownerSet(const char* name) {
  _safeCopy(_ownerName, sizeof(_ownerName), name);
  _prefs.begin("buddy", false);
  _prefs.putString("owner", _ownerName);
  _prefs.end();
}

const char* ownerName() { return _ownerName; }

uint8_t speciesIdxLoad() {
  _prefs.begin("buddy", true);
  uint8_t v = _prefs.getUChar("species", 0xFF);
  _prefs.end();
  return v;
}

void speciesIdxSave(uint8_t idx) {
  _prefs.begin("buddy", false);
  _prefs.putUChar("species", idx);
  _prefs.end();
}

void statsFactoryReset() {
  _prefs.begin("buddy", false);
  _prefs.clear();
  _prefs.end();
}
