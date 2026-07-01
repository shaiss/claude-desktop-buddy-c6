#pragma once
#include <stdint.h>

// GIF character playback (upstream character.h). Interface is final; the
// implementation lands in M6 — until then characterInit() reports no
// character and the default palette drives UI colors.

struct Palette {
  uint16_t body, bg, text, textDim, ink;
};

// Reads /characters/<name>/manifest.json (nullptr = scan /characters/ for
// the first installed directory), parses colors, caches GIF paths.
bool characterInit(const char* name);
bool characterLoaded();

// 0..6: sleep, idle, busy, attention, celebrate, dizzy, heart.
// Closes the current GIF, opens the one for this state. No-op if same state.
void characterSetState(uint8_t state);

// Advances timing; decodes the next frame into the sprite when due.
void characterTick();
void characterInvalidate();
void characterClose();   // close GIF + clear loaded flag; FS stays mounted

const Palette& characterPalette();
