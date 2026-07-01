#pragma once
#include <stdint.h>

// Multi-species ASCII buddy renderer — upstream's buddy.h + buddy_common.h
// merged. Each species lives in buddies/<name>.cpp and exposes 7 state
// functions matching PersonaState order: sleep, idle, busy, attention,
// celebrate, dizzy, heart. Species draw ONLY via the buddyPrint* helpers.

void buddyInit();
void buddyTick(uint8_t personaState);
void buddyInvalidate();
void buddySetSpecies(const char* name);
void buddySetSpeciesIdx(uint8_t idx);
void buddyNextSpecies();
void buddySetPeek(bool peek);
uint8_t buddySpeciesIdx();
uint8_t buddySpeciesCount();
const char* buddySpeciesName();

// Per-species state function: takes the global tick count and renders the
// buddy + overlays for the current state into the shared sprite.
typedef void (*StateFn)(uint32_t t);

struct Species {
  const char* name;
  uint16_t bodyColor;
  StateFn states[7];   // index by PersonaState (0=sleep .. 6=heart)
};

// ---- shared geometry (172-wide screen; upstream was 135) ----
extern const int BUDDY_X_CENTER;
extern const int BUDDY_CANVAS_W;
extern const int BUDDY_Y_BASE;
extern const int BUDDY_Y_OVERLAY;
extern const int BUDDY_CHAR_W;
extern const int BUDDY_CHAR_H;

// ---- common colors species can use freely ----
extern const uint16_t BUDDY_BG;
extern const uint16_t BUDDY_HEART;
extern const uint16_t BUDDY_DIM;
extern const uint16_t BUDDY_YEL;
extern const uint16_t BUDDY_WHITE;
extern const uint16_t BUDDY_CYAN;
extern const uint16_t BUDDY_GREEN;
extern const uint16_t BUDDY_PURPLE;
extern const uint16_t BUDDY_RED;
extern const uint16_t BUDDY_BLUE;

// Print one line centered around BUDDY_X_CENTER, optionally x-offset.
void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff = 0);

// Print N-line sprite block. yOffset is added to BUDDY_Y_BASE for the top row.
void buddyPrintSprite(const char* const* lines, uint8_t nLines, int yOffset,
                      uint16_t color, int xOff = 0);

// Set sprite text color/cursor directly (for ad-hoc particle drawing).
void buddySetCursor(int x, int y);
void buddySetColor(uint16_t fg);
void buddyPrint(const char* s);
