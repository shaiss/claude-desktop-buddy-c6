#include <Arduino.h>

// M1: prove the toolchain end to end — boot banner on the native USB
// Serial/JTAG console and the WS2812 cycling red → green → blue.

static const int RGB_PIN = 8;   // strap pin: first write happens inside loop(), after boot

void setup() {
  Serial.begin(115200);
  // Native USB CDC: give the host a moment to enumerate so the banner isn't dropped.
  delay(2000);
  Serial.println("[boot] claude-desktop-buddy-c6 M1");
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
