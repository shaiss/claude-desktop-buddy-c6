#include <Arduino.h>
#include <esp_mac.h>
#include <LittleFS.h>
#include <string.h>
#include <stdarg.h>
#include "display.h"
#include "ble_bridge.h"
#include "state.h"
#include "stats.h"
#include "led.h"
#include "characters/ascii_pets.h"
#include "characters/gif_player.h"

// GIF (installed character) vs ASCII species mode. species NVS: 0..N-1 =
// ASCII index, 0xFF = use the installed GIF (also the fresh-install default).
bool buddyMode = true;
bool gifAvailable = false;
static const uint8_t SPECIES_GIF = 0xFF;

static char btName[16] = "Claude";

// Approval prompt bookkeeping (decision buttons land in M7; the panel and
// the arrival edge are wired now).
static char     lastPromptId[40] = "";
static uint32_t promptArrivedMs = 0;
static bool     responseSent = false;

// BLE-driven sleep: dim the backlight and count nap time while P_SLEEP.
static bool     napping = false;
static uint32_t napStartMs = 0;

static uint8_t  msgScroll = 0;
static uint16_t lastLineGen = 0;

// ---------------------------------------------------------------------------
// HUD / panels
// ---------------------------------------------------------------------------

// Greedy word-wrap into fixed-width rows; continuation rows get a leading
// space (upstream wrapInto, widened for the 172px screen).
static const uint8_t WRAP_W = 27;
static uint8_t wrapInto(const char* in, char out[][30], uint8_t maxRows, uint8_t width) {
  uint8_t row = 0, col = 0;
  const char* p = in;
  while (*p && row < maxRows) {
    while (*p == ' ') p++;
    const char* w = p;
    while (*p && *p != ' ') p++;
    uint8_t wlen = p - w;
    if (wlen == 0) break;
    uint8_t need = (col > 0 ? 1 : 0) + wlen;
    if (col + need > width) {
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;
    }
    if (col > 1 || (col == 1 && out[row][0] != ' ')) out[row][col++] = ' ';
    else if (col == 1 && row > 0) {}
    while (wlen > width - col) {
      uint8_t take = width - col;
      memcpy(&out[row][col], w, take); col += take; w += take; wlen -= take;
      out[row][col] = 0;
      if (++row >= maxRows) return row;
      out[row][0] = ' '; col = 1;
    }
    memcpy(&out[row][col], w, wlen); col += wlen;
  }
  if (col > 0 && row < maxRows) { out[row][col] = 0; row++; }
  return row;
}

// Status strip: state | sessions | link | level. New surface (we have 80
// extra vertical pixels vs upstream).
static void drawStatusStrip() {
  const Palette& p = characterPalette();
  spr.fillRect(0, CHAR_REGION_H, SCREEN_W, HUD_TOP - CHAR_REGION_H, p.bg);
  spr.drawFastHLine(0, CHAR_REGION_H, SCREEN_W, p.textDim);
  spr.setTextSize(1);
  int y = STATUS_STRIP_Y + 2;

  static const uint16_t stateCol[7] = {
    0x8410, 0xFFFF, 0x07FF, 0xFFE0, 0x07E0, 0xF800, 0xF810
  };
  PersonaState st = stateActive();
  spr.setTextColor(stateCol[st], p.bg);
  spr.setCursor(4, y);
  spr.print(STATE_NAMES[st]);

  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(70, y);
  spr.printf("%u/%u/%u", tama.sessionsTotal, tama.sessionsRunning, tama.sessionsWaiting);

  const char* link = bleSecure() ? "enc" : (bleConnected() ? "ble" : (dataConnected() ? "usb" : "adv"));
  spr.setTextColor(bleConnected() || dataConnected() ? 0x07E0 : p.textDim, p.bg);
  spr.setCursor(112, y);
  spr.print(link);

  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(140, y);
  spr.printf("lv%u", stats().level);
}

static void drawApproval() {
  const Palette& p = characterPalette();
  const int AREA = 100;
  spr.fillRect(0, SCREEN_H - AREA, SCREEN_W, AREA, p.bg);
  spr.drawFastHLine(0, SCREEN_H - AREA, SCREEN_W, p.textDim);
  const uint16_t HOT = 0xFA20;

  spr.setTextSize(1);
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(4, SCREEN_H - AREA + 4);
  uint32_t waited = (millis() - promptArrivedMs) / 1000;
  if (waited >= 10) spr.setTextColor(HOT, p.bg);
  spr.printf("approve? %lus", (unsigned long)waited);

  // Tool name big if it fits one line at size 2 (14 glyphs * 12px on 172)
  int toolLen = strlen(tama.promptTool);
  spr.setTextColor(p.text, p.bg);
  spr.setTextSize(toolLen <= 14 ? 2 : 1);
  spr.setCursor(4, SCREEN_H - AREA + (toolLen <= 14 ? 16 : 20));
  spr.print(tama.promptTool);
  spr.setTextSize(1);

  spr.setTextColor(p.textDim, p.bg);
  int hlen = strlen(tama.promptHint);
  spr.setCursor(4, SCREEN_H - AREA + 38);
  spr.printf("%.28s", tama.promptHint);
  if (hlen > 28) {
    spr.setCursor(4, SCREEN_H - AREA + 48);
    spr.printf("%.28s", tama.promptHint + 28);
  }

  if (responseSent) {
    spr.setTextColor(p.textDim, p.bg);
    spr.setCursor(4, SCREEN_H - 12);
    spr.print("sent...");
  } else {
    spr.setTextColor(0x07E0, p.bg);
    spr.setCursor(4, SCREEN_H - 12);
    spr.print("BOOT: approve");
    spr.setTextColor(HOT, p.bg);
    spr.setCursor(SCREEN_W - 60, SCREEN_H - 12);
    spr.print("hold: deny");
  }
}

// Recent-message area: wrapped transcript lines, newest at the bottom
// (upstream drawHUD with more rows — the tall screen's dividend).
static void drawHUD() {
  if (tama.promptId[0]) { drawApproval(); return; }
  if (!settings().hud) return;
  const Palette& p = characterPalette();
  const int LH = 8;
  const int SHOW = (SCREEN_H - HUD_TOP - 4) / LH;   // ~20 rows
  spr.fillRect(0, HUD_TOP, SCREEN_W, SCREEN_H - HUD_TOP, p.bg);
  spr.setTextSize(1);

  if (tama.lineGen != lastLineGen) { msgScroll = 0; lastLineGen = tama.lineGen; }

  if (tama.nLines == 0) {
    spr.setTextColor(p.text, p.bg);
    spr.setCursor(4, SCREEN_H - LH - 2);
    spr.print(tama.msg);
    return;
  }

  static char disp[32][30];
  static uint8_t srcOf[32];
  uint8_t nDisp = 0;
  for (uint8_t i = 0; i < tama.nLines && nDisp < 32; i++) {
    uint8_t got = wrapInto(tama.lines[i], &disp[nDisp], 32 - nDisp, WRAP_W);
    for (uint8_t j = 0; j < got; j++) srcOf[nDisp + j] = i;
    nDisp += got;
  }

  uint8_t maxBack = (nDisp > SHOW) ? (nDisp - SHOW) : 0;
  if (msgScroll > maxBack) msgScroll = maxBack;

  int end = (int)nDisp - msgScroll;
  int start = end - SHOW; if (start < 0) start = 0;
  uint8_t newest = tama.nLines - 1;
  int rows = end - start;
  for (int i = 0; i < rows; i++) {
    uint8_t row = start + i;
    bool fresh = (srcOf[row] == newest) && (msgScroll == 0);
    spr.setTextColor(fresh ? p.text : p.textDim, p.bg);
    // Anchor to the bottom so the newest row hugs the screen edge.
    spr.setCursor(4, SCREEN_H - 4 - (rows - i) * LH);
    spr.print(disp[row]);
  }
}

static void drawPasskey() {
  const Palette& p = characterPalette();
  spr.fillSprite(p.bg);
  spr.setTextSize(1);
  spr.setTextColor(p.textDim, p.bg);
  spr.setCursor(10, 100);  spr.print("BLUETOOTH PAIRING");
  spr.setCursor(10, 220);  spr.print("enter on desktop:");
  spr.setTextSize(3);
  spr.setTextColor(p.text, p.bg);
  char b[8];
  snprintf(b, sizeof(b), "%06lu", (unsigned long)blePasskey());
  spr.setCursor((SCREEN_W - 18 * 6) / 2, 150);
  spr.print(b);
}

// Install progress / no-character screen (only reachable in GIF mode).
static void drawNoCharacter() {
  const Palette& p = characterPalette();
  spr.fillSprite(p.bg);
  spr.setTextColor(p.textDim, p.bg);
  spr.setTextSize(1);
  if (xferActive()) {
    uint32_t done = xferProgress(), total = xferTotal();
    spr.setCursor(8, 130); spr.print("installing");
    spr.setCursor(8, 142); spr.printf("%luK / %luK", done / 1024, total / 1024);
    int barW = SCREEN_W - 16;
    spr.drawRect(8, 156, barW, 8, p.textDim);
    if (total > 0) {
      int fill = (int)((uint64_t)barW * done / total);
      if (fill > 1) spr.fillRect(9, 157, fill - 1, 6, p.body);
    }
  } else {
    spr.setCursor(8, 140);
    spr.print("no character loaded");
  }
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("[boot] claude-desktop-buddy-c6 M4");

  bool dispOk = displayInit();
  statsLoad();
  settingsLoad();
  petNameLoad();
  buddyInit();
  ledInit();

  // Format-on-fail: first boot has a blank littlefs partition. The partition
  // LABEL must be passed — begin() defaults to looking for one named "spiffs"
  // and ours is named "littlefs" in partitions.csv.
  if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
    Serial.println("[fs] mount FAILED");
  }

  characterInit(nullptr);   // scan /characters/ for whatever is installed
  gifAvailable = characterLoaded();
  buddyMode = !(gifAvailable && speciesIdxLoad() == SPECIES_GIF);

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-%02X%02X", mac[4], mac[5]);
  bleInit(btName);

  // Boot splash (upstream greeting).
  {
    const Palette& p = characterPalette();
    spr.fillSprite(p.bg);
    spr.setTextDatum(MC_DATUM);
    spr.setTextSize(2);
    if (ownerName()[0]) {
      char line[40];
      snprintf(line, sizeof(line), "%s's", ownerName());
      spr.setTextColor(p.text, p.bg);  spr.drawString(line, SCREEN_W / 2, SCREEN_H / 2 - 14);
      spr.setTextColor(p.body, p.bg);  spr.drawString(petName(), SCREEN_W / 2, SCREEN_H / 2 + 14);
    } else {
      spr.setTextColor(p.body, p.bg);  spr.drawString("Hello!", SCREEN_W / 2, SCREEN_H / 2 - 14);
      spr.setTextSize(1);
      spr.setTextColor(p.textDim, p.bg);
      spr.drawString("a buddy appears", SCREEN_W / 2, SCREEN_H / 2 + 14);
    }
    spr.setTextDatum(TL_DATUM);
    spr.setTextSize(1);
    spr.pushSprite(0, 0);
    delay(1800);
  }

  Serial.printf("[boot] %s, display=%d heap=%u species=%s\n",
                buddyMode ? "ASCII mode" : "GIF character loaded",
                (int)dispOk, ESP.getFreeHeap(), buddySpeciesName());
}

void loop() {
  uint32_t now = millis();

  bridgePoll();
  if (statsPollLevelUp()) stateTriggerOneShot(P_CELEBRATE, 3000);
  stateTick();

  // Prompt arrival edge: reset the response latch, log for verification.
  if (strcmp(tama.promptId, lastPromptId) != 0) {
    strncpy(lastPromptId, tama.promptId, sizeof(lastPromptId) - 1);
    lastPromptId[sizeof(lastPromptId) - 1] = 0;
    responseSent = false;
    if (tama.promptId[0]) {
      promptArrivedMs = now;
      Serial.printf("[prompt] id=%s tool=%s hint=%s\n",
                    tama.promptId, tama.promptTool, tama.promptHint);
    }
  }

  PersonaState st = stateActive();
  ledTick(st);

  // Sleep = dim backlight + accumulate nap stats. Wake restores instantly.
  if (st == P_SLEEP && !napping) {
    napping = true;
    napStartMs = now;
    lcd.setBrightness(30);
  } else if (st != P_SLEEP && napping) {
    napping = false;
    statsOnNapEnd((now - napStartMs) / 1000);
    statsOnWake();
    lcd.setBrightness(200);
  }

  // Character region
  if (buddyMode) {
    buddyTick(st);
  } else if (characterLoaded()) {
    characterSetState(st);
    characterTick();
  } else {
    drawNoCharacter();
  }

  // Overlays / panels
  if (blePasskey()) {
    drawPasskey();
  } else {
    drawStatusStrip();
    drawHUD();
  }
  spr.pushSprite(0, 0);

  // Liveness heartbeat for autonomous verification
  static uint32_t lastAlive = 0;
  if (now - lastAlive >= 10000) {
    lastAlive = now;
    Serial.printf("[alive] up=%lus heap=%u state=%s conn=%d ble=%d\n",
                  (unsigned long)(now / 1000), ESP.getFreeHeap(),
                  STATE_NAMES[st], (int)tama.connected, (int)bleConnected());
  }

  delay(16);
}
