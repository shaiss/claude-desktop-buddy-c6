#include <Arduino.h>
#include <esp_mac.h>
#include "display.h"
#include "ble_bridge.h"

// M3: NUS echo. Advertise Claude-XXXX, echo anything written to RX back
// out on TX with "\r\n" appended. M2 test pattern stays up with a live
// BLE status line; passkey shows full-screen during pairing.

static const int RGB_PIN = 8;
static char btName[16] = "Claude";

static void drawTestPattern() {
  spr.fillSprite(TFT_BLACK);
  spr.drawRect(0, 0, SCREEN_W, SCREEN_H, TFT_WHITE);

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

  spr.fillCircle(SCREEN_W / 2, 190, 40, TFT_YELLOW);
  spr.setTextSize(2);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  spr.setCursor((SCREEN_W - 5 * 12) / 2, 250);
  spr.print("buddy");
}

static void drawStatus() {
  spr.fillRect(1, SCREEN_H - 40, SCREEN_W - 2, 39, TFT_BLACK);
  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.setCursor(6, SCREEN_H - 36);
  spr.print(btName);
  spr.setTextColor(bleConnected() ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
  spr.setCursor(6, SCREEN_H - 24);
  spr.print(bleConnected() ? (bleSecure() ? "linked (encrypted)" : "linked") : "advertising...");
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.setCursor(6, SCREEN_H - 12);
  spr.print("172x320 ofs=34");
}

static void drawPasskey() {
  spr.fillSprite(TFT_BLACK);
  spr.setTextSize(1);
  spr.setTextColor(TFT_DARKGREY, TFT_BLACK);
  spr.setCursor(10, 90);  spr.print("BLUETOOTH PAIRING");
  spr.setCursor(10, 230); spr.print("enter on desktop:");
  spr.setTextSize(3);
  spr.setTextColor(TFT_WHITE, TFT_BLACK);
  char b[8];
  snprintf(b, sizeof(b), "%06lu", (unsigned long)blePasskey());
  spr.setCursor((SCREEN_W - 18 * 6) / 2, 150);
  spr.print(b);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("[boot] claude-desktop-buddy-c6 M3");

  bool ok = displayInit();
  Serial.printf("[m3] display %s heap=%u\n", ok ? "ok" : "SPRITE FAIL", ESP.getFreeHeap());
  drawTestPattern();

  // Advertise as Claude-XXXX (last two BT MAC bytes) per REFERENCE.md so
  // multiple devices stay distinguishable in the desktop picker.
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
  Serial.printf("[m3] ble up, heap=%u\n", ESP.getFreeHeap());
}

void loop() {
  uint32_t now = millis();

  // Echo: drain whatever arrived on RX, send it back with \r\n appended.
  static uint8_t echo[512];
  size_t n = 0;
  while (bleAvailable() && n < sizeof(echo) - 2) {
    int c = bleRead();
    if (c < 0) break;
    echo[n++] = (uint8_t)c;
  }
  if (n > 0) {
    Serial.printf("[echo] %u bytes: %.*s\n", (unsigned)n, (int)n, (const char*)echo);
    echo[n++] = '\r';
    echo[n++] = '\n';
    bleWrite(echo, n);
  }

  // LED heartbeat (M1 behavior, slower): blue when linked, dim white otherwise.
  static uint32_t lastLed = 0;
  static bool ledOn = false;
  if (now - lastLed >= 1000) {
    lastLed = now;
    ledOn = !ledOn;
    if (ledOn) rgbLedWrite(RGB_PIN, bleConnected() ? 0 : 16, bleConnected() ? 0 : 16, bleConnected() ? 48 : 16);
    else       rgbLedWrite(RGB_PIN, 0, 0, 0);
  }

  // Screen: passkey takes over during pairing, else pattern + status.
  static bool wasPasskey = false;
  bool pk = blePasskey() != 0;
  if (pk != wasPasskey) {
    wasPasskey = pk;
    if (!pk) drawTestPattern();
  }
  if (pk) drawPasskey();
  else    drawStatus();
  spr.pushSprite(0, 0);

  // Serial liveness every 5s
  static uint32_t lastAlive = 0;
  if (now - lastAlive >= 5000) {
    lastAlive = now;
    Serial.printf("[alive] up=%lus heap=%u ble=%s%s\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(),
                  bleConnected() ? "linked" : "adv",
                  bleSecure() ? "+enc" : "");
  }

  delay(16);
}
