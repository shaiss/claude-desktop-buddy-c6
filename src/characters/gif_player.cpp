#include "gif_player.h"
#include <Arduino.h>

// M4 stub — real AnimatedGIF + LittleFS implementation lands in M6.
// Default palette matches upstream's (capybara-brown body on black).

static Palette pal = { 0xC2A6, 0x0000, 0xFFFF, 0x8410, 0x0000 };

bool characterInit(const char* name) {
  Serial.println("[char] gif player not built yet (M6)");
  return false;
}

bool characterLoaded() { return false; }
void characterSetState(uint8_t) {}
void characterTick() {}
void characterInvalidate() {}
void characterClose() {}
const Palette& characterPalette() { return pal; }
