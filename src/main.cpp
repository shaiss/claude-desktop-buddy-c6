#include <Arduino.h>
#include "display.h"

// M2: display bring-up. Self-describing test pattern so a glance verifies
// offset (1px border on all four edges), color order (labeled bars), and
// the sprite pipeline (everything drawn via spr, pushed once).
// LED cycle from M1 kept running so both are visible at once.

static const int RGB_PIN = 8;

static void drawTestPattern() {
  spr.fillSprite(TFT_BLACK);

  // Border: exactly 1px inside every edge. If offset_x is wrong this is
  // clipped or floating — the single clearest offset check.
  spr.drawRect(0, 0, SCREEN_W, SCREEN_H, TFT_WHITE);

  // Labeled color bars: if the panel is BGR or inversion is off, the
  // label under each bar won't match what the eye sees.
  struct { uint16_t c; const char* name; } bars[] = {
    { TFT_RED, "RED" }, { TFT_GREEN, "GREEN" }, { TFT_BLUE, "BLUE" },
  };
  const int bw = 48, bh = 70, gap = 6;
  int x = (SCREEN_W - 3 * bw - 2 * gap) / 2;
  for (auto& b : bars) {
    spr.fillRect(x, 24, bw, bh, b.c);
    spr.setTextColor(TFT_WHITE, TFT_BLACK);
    spr.setTextSize(1);
    spr.setCursor(x + (bw - (int)strlen(b.name) * 6) / 2, 24 + bh + 6);
    spr.print(b.name);
    x += bw + gap;
  }

  // Filled circle + centered text
  spr.fillCircle(SCREEN_W / 2, 190, 40, TFT_YELLOW);
  spr.setTextSize(2);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setCursor((SCREEN_W - 5 * 12) / 2, 250);   // "buddy" = 5 glyphs at size 2
  spr.print("buddy");

  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.setCursor(6, SCREEN_H - 12);
  spr.printf("172x320 ofs=34");

  spr.pushSprite(0, 0);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("[boot] claude-desktop-buddy-c6 M2");
  Serial.printf("[m2] heap before display: %u\n", ESP.getFreeHeap());
  bool ok = displayInit();
  Serial.printf("[m2] displayInit %s, heap after sprite: %u\n",
                ok ? "ok" : "SPRITE ALLOC FAILED", ESP.getFreeHeap());
  if (ok) {
    drawTestPattern();
    Serial.println("[m2] test pattern pushed");
  } else {
    // Sprite failed: draw direct so the panel still shows life.
    lcd.fillScreen(TFT_RED);
  }
}

void loop() {
  static const struct { uint8_t r, g, b; char tag; } SEQ[] = {
    { 48, 0, 0, 'R' }, { 0, 48, 0, 'G' }, { 0, 0, 48, 'B' },
  };
  static uint8_t i = 0;
  rgbLedWrite(RGB_PIN, SEQ[i].r, SEQ[i].g, SEQ[i].b);
  Serial.printf("[led] %c up=%lus heap=%u\n", SEQ[i].tag,
                (unsigned long)(millis() / 1000), ESP.getFreeHeap());
  i = (i + 1) % 3;
  delay(500);
}
