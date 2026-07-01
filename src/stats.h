#pragma once
#include <stdint.h>

// NVS-backed stats + settings, ported from upstream stats.h (de-headerized).
// Namespace "buddy", same keys. Save on events only — never on a timer
// (NVS sectors have ~100K write cycles).

static const uint32_t TOKENS_PER_LEVEL = 50000;

struct Stats {
  uint32_t napSeconds;       // cumulative sleep time (upstream: face-down naps)
  uint16_t approvals;
  uint16_t denials;
  uint16_t velocity[8];      // ring: seconds-to-respond per approval
  uint8_t  velIdx;
  uint8_t  velCount;
  uint8_t  level;
  uint32_t tokens;           // cumulative output tokens, drives level
};

struct Settings {
  bool led;
  bool hud;
};

void statsLoad();
void statsSave();
void statsOnApproval(uint32_t secondsToRespond);
void statsOnDenial();
void statsOnBridgeTokens(uint32_t bridgeTotal);
bool statsPollLevelUp();
void statsOnNapEnd(uint32_t seconds);
void statsOnWake();
uint16_t statsMedianVelocity();
uint8_t statsMoodTier();      // 0..4
uint8_t statsEnergyTier();    // 0..5
uint8_t statsFedProgress();   // 0..9 pips within current level

void settingsLoad();
void settingsSave();
Settings& settings();
const Stats& stats();

void petNameLoad();
void petNameSet(const char* name);
const char* petName();
void ownerSet(const char* name);
const char* ownerName();

uint8_t speciesIdxLoad();     // 0xFF = GIF mode sentinel (or unset)
void speciesIdxSave(uint8_t idx);

void statsFactoryReset();     // clear the whole NVS namespace
