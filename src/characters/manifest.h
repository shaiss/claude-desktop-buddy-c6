#pragma once
#include <stdint.h>
#include "gif_player.h"   // Palette

// Character pack manifest (/characters/<name>/manifest.json), matching
// upstream's format exactly:
//   { "name": "...", "colors": {body,bg,text,textDim,ink hex strings},
//     "states": { "<state>": "file.gif" | ["a.gif","b.gif"], ... } }
// plus the alternative "mode":"text" where states carry {frames:[...],delay:N}.
// State keys follow PersonaState order: sleep idle busy attention celebrate
// dizzy heart. Array values rotate (idle carousel).

constexpr uint8_t MANIFEST_MAX_GIFS = 32;
constexpr uint8_t MANIFEST_N_STATES = 7;

extern const char* const MANIFEST_STATE_NAMES[MANIFEST_N_STATES];

struct TextState {
  char     frames[8][20];
  uint8_t  nFrames;
  uint16_t delayMs;
};

struct Manifest {
  char    name[24];
  Palette colors;
  bool    textMode;
  // GIF mode: flat filename list, indexed per state by [start, start+count)
  char    gifPaths[MANIFEST_MAX_GIFS][32];
  uint8_t gifTotal;
  uint8_t stateStart[MANIFEST_N_STATES];
  uint8_t stateCount[MANIFEST_N_STATES];
  // text mode
  TextState textStates[MANIFEST_N_STATES];
};

// Parse /characters/<dirName>/manifest.json from LittleFS. Missing color
// fields keep the values already in out->colors (caller seeds defaults).
bool manifestLoad(const char* dirName, Manifest* out);
