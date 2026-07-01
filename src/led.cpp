#include "led.h"
#include <Arduino.h>
#include "state.h"
#include "stats.h"

static const int RGB_PIN = 8;   // strap pin — first write must be after boot

// 0..255 triangle wave with the given period.
static uint8_t tri(uint32_t now, uint32_t periodMs) {
  uint32_t ph = (now % periodMs) * 512 / periodMs;
  return (uint8_t)(ph < 256 ? ph : 511 - ph);
}

void ledInit() {
  rgbLedWrite(RGB_PIN, 0, 0, 0);
}

void ledTick(uint8_t s) {
  static uint8_t lastR = 0xFF, lastG = 0xFF, lastB = 0xFF;
  uint32_t now = millis();
  uint8_t r = 0, g = 0, b = 0;

  if (settings().led) {
    switch (s) {
      case P_BUSY: {                     // blue pulse, 2s breathe
        uint8_t v = tri(now, 2000);
        b = 8 + (v * 40 / 255);
        break;
      }
      case P_ATTENTION: {                // amber blink, upstream's 400ms LED cadence
        bool on = (now / 400) % 2;
        if (on) { r = 48; g = 24; }
        break;
      }
      case P_CELEBRATE: {                // green sweep: quick rise, snap off
        uint32_t ph = now % 900;
        g = (uint8_t)(ph * 48 / 900);
        break;
      }
      case P_DIZZY: {                    // red flicker, pseudo-random
        uint32_t h = (now / 90) * 2654435761u;
        r = 8 + ((h >> 24) % 40);
        break;
      }
      case P_HEART: {                    // soft pink pulse
        uint8_t v = tri(now, 1600);
        r = 6 + (v * 42 / 255);
        b = 2 + (v * 12 / 255);
        g = v * 6 / 255;
        break;
      }
      default: break;                    // sleep / idle: off
    }
  }

  // WS2812 writes bit-bang via RMT; skip when nothing changed.
  if (r != lastR || g != lastG || b != lastB) {
    rgbLedWrite(RGB_PIN, r, g, b);
    lastR = r; lastG = g; lastB = b;
  }
}
