#pragma once
#include <stdint.h>

// WS2812 status cues (new capability on this board, not in upstream):
//   sleep/idle  off
//   busy        blue pulse
//   attention   amber blink
//   celebrate   green sweep
//   dizzy       red flicker
//   heart       soft pink pulse
// Kept dim (<=48/255) — a desk gadget, not a flashlight.

void ledInit();
void ledTick(uint8_t personaState);
