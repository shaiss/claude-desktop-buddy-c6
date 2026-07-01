#pragma once
#include <stdint.h>

// The 7 persona states, in protocol/manifest order — this ordering is the
// contract shared with GIF manifests and species animation tables.
enum PersonaState : uint8_t {
  P_SLEEP, P_IDLE, P_BUSY, P_ATTENTION, P_CELEBRATE, P_DIZZY, P_HEART
};
extern const char* const STATE_NAMES[7];

// Live snapshot of what the desktop bridge reports (upstream TamaState).
struct TamaState {
  uint8_t  sessionsTotal;
  uint8_t  sessionsRunning;
  uint8_t  sessionsWaiting;
  bool     recentlyCompleted;
  uint32_t tokensToday;
  uint32_t lastUpdated;
  char     msg[24];
  bool     connected;
  char     lines[8][92];
  uint8_t  nLines;
  uint16_t lineGen;          // bumps when lines change — lets UI reset scroll
  char     promptId[40];     // pending permission request ID; empty = none
  char     promptTool[20];
  char     promptHint[44];
};

extern TamaState tama;

// Liveness bookkeeping (fed by the bridge on every parsed JSON line).
void dataMarkLive();
bool dataConnected();        // JSON arrived within the last 30s
void dataMarkRtcValid();
bool dataRtcValid();

// Demo mode: auto-cycle fake scenarios every 8s, ignoring live data.
void dataSetDemo(bool on);
bool dataDemo();
void dataDemoTick();         // no-op unless demo is on
const char* dataScenarioName();

// FSM. stateTick() derives base state from tama (upstream rules, with our
// BLE-driven sleep delta), resolves one-shot overrides, logs transitions.
void stateTriggerOneShot(PersonaState s, uint32_t durMs);
bool stateOneShotActive();
PersonaState stateActive();
void stateTick();
