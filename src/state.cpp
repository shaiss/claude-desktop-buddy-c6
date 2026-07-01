#include "state.h"
#include <Arduino.h>
#include <string.h>
#include "ble_bridge.h"

const char* const STATE_NAMES[7] = {
  "sleep", "idle", "busy", "attention", "celebrate", "dizzy", "heart"
};

TamaState tama;

// --- liveness ----------------------------------------------------------------

static uint32_t _lastLiveMs = 0;
static bool     _rtcValid = false;

void dataMarkLive()     { _lastLiveMs = millis(); }
void dataMarkRtcValid() { _rtcValid = true; }
bool dataRtcValid()     { return _rtcValid; }

bool dataConnected() {
  return _lastLiveMs != 0 && (millis() - _lastLiveMs) <= 30000;
}

// --- demo mode (upstream's fake scenario carousel) ----------------------------

struct _Fake { const char* n; uint8_t t, r, w; bool c; uint32_t tok; };
static const _Fake _FAKES[] = {
  {"asleep", 0, 0, 0, false, 0}, {"one idle", 1, 0, 0, false, 12000},
  {"busy", 4, 3, 0, false, 89000}, {"attention", 2, 1, 1, false, 45000},
  {"completed", 1, 0, 0, true, 142000},
};

static bool     _demoMode = false;
static uint8_t  _demoIdx = 0;
static uint32_t _demoNext = 0;

void dataSetDemo(bool on) {
  _demoMode = on;
  if (on) { _demoIdx = 0; _demoNext = millis(); }
}
bool dataDemo() { return _demoMode; }

void dataDemoTick() {
  if (!_demoMode) return;
  uint32_t now = millis();
  if (now >= _demoNext) { _demoIdx = (_demoIdx + 1) % 5; _demoNext = now + 8000; }
  const _Fake& s = _FAKES[_demoIdx];
  tama.sessionsTotal = s.t; tama.sessionsRunning = s.r; tama.sessionsWaiting = s.w;
  tama.recentlyCompleted = s.c; tama.tokensToday = s.tok; tama.lastUpdated = now;
  tama.connected = true;
  snprintf(tama.msg, sizeof(tama.msg), "demo: %s", s.n);
}

const char* dataScenarioName() {
  if (_demoMode) return _FAKES[_demoIdx].n;
  if (dataConnected()) return "live";
  return "none";
}

// --- FSM ----------------------------------------------------------------------

static PersonaState baseState = P_SLEEP;
static PersonaState activeState = P_SLEEP;
static uint32_t oneShotUntil = 0;
static uint32_t lastAwakeMs = 0;
static const uint32_t SLEEP_AFTER_MS = 30000;

// Upstream derive(), connected path verbatim (note: busy needs running >= 3).
static PersonaState derive(const TamaState& s) {
  if (!s.connected)            return P_IDLE;
  if (s.sessionsWaiting > 0)   return P_ATTENTION;
  if (s.recentlyCompleted)     return P_CELEBRATE;
  if (s.sessionsRunning >= 3)  return P_BUSY;
  return P_IDLE;
}

void stateTriggerOneShot(PersonaState s, uint32_t durMs) {
  if (activeState != s) {
    Serial.printf("[state] %s -> %s (one-shot %lums)\n",
                  STATE_NAMES[activeState], STATE_NAMES[s], (unsigned long)durMs);
  }
  activeState = s;
  oneShotUntil = millis() + durMs;
}

bool stateOneShotActive() {
  return (int32_t)(millis() - oneShotUntil) < 0;
}

PersonaState stateActive() { return activeState; }

void stateTick() {
  uint32_t now = millis();

  // Our delta vs upstream: sleep is BLE-connection-driven. A connected
  // central (even a silent one) or recent JSON keeps the pet awake;
  // 30s with neither -> sleep. Upstream instead turned the screen off on
  // an idle timer and had no persistent sleep state at all.
  if (bleConnected() || dataConnected() || dataDemo()) lastAwakeMs = now;

  bool asleep = (now - lastAwakeMs) > SLEEP_AFTER_MS;
  baseState = asleep ? P_SLEEP : derive(tama);

  if ((int32_t)(now - oneShotUntil) >= 0) {
    if (activeState != baseState) {
      Serial.printf("[state] %s -> %s (t=%u r=%u w=%u conn=%d ble=%d)\n",
                    STATE_NAMES[activeState], STATE_NAMES[baseState],
                    tama.sessionsTotal, tama.sessionsRunning, tama.sessionsWaiting,
                    (int)tama.connected, (int)bleConnected());
    }
    activeState = baseState;
  }
}
