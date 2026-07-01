#include "gif_player.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <AnimatedGIF.h>
#include <string.h>
#include "manifest.h"
#include "../display.h"

// Upstream character.cpp, split: manifest parsing lives in manifest.cpp,
// this file owns the AnimatedGIF pipeline and playback pacing.

static bool     loaded = false;
static Palette  pal = { 0xC2A6, 0x0000, 0xFFFF, 0x8410, 0x0000 };
static Manifest mf;
static char     basePath[48];
static uint8_t  stateRot[MANIFEST_N_STATES];
static uint8_t  curState = 0xFF;

static uint8_t  textFrame = 0;
static uint32_t textNext = 0;

static AnimatedGIF gif;
static File        gifFile;
static int         gifX = 0, gifY = 0, gifW = 0, gifH = 0;
static uint32_t    nextFrameAt = 0;
static uint32_t    animPauseUntil = 0;
static uint32_t    variantStartedMs = 0;
static const uint32_t VARIANT_DWELL_MS = 5000;
static const uint32_t ANIM_PAUSE_MS    = 800;
static bool        gifOpen = false;

// Center the GIF in the character region (96px-wide art on a 172px screen).
static void gifPlace() {
  gifX = (SCREEN_W - gifW) / 2;
  gifY = (CHAR_REGION_H - gifH) / 2;
  if (gifY < 0) gifY = 0;
}

// --- AnimatedGIF file callbacks (LittleFS) ---------------------------------

static void* gifOpenCb(const char* fname, int32_t* pSize) {
  gifFile = LittleFS.open(fname, "r");
  if (!gifFile) return nullptr;
  *pSize = gifFile.size();
  return (void*)&gifFile;
}

static void gifCloseCb(void* handle) {
  File* f = (File*)handle;
  if (f) f->close();
}

static int32_t gifReadCb(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
  File* f = (File*)pFile->fHandle;
  int32_t n = f->read(pBuf, iLen);
  pFile->iPos = f->position();
  return n;
}

static int32_t gifSeekCb(GIFFILE* pFile, int32_t iPosition) {
  File* f = (File*)pFile->fHandle;
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}

// Draw callback: one scanline, palette lookup per pixel. Transparent pixels
// get the character's bg so each frame fully paints its region — packs are
// prepped unoptimized full-frame (no disposal/delta handling, by design;
// upstream tried the delta path and reverted it).
static void gifDrawCb(GIFDRAW* d) {
  uint16_t* pal16 = d->pPalette;
  uint8_t*  src   = d->pPixels;
  uint8_t   t     = d->ucTransparent;
  bool      hasT  = d->ucHasTransparency;

  int y = gifY + d->iY + d->y;
  if (y < 0 || y >= CHAR_REGION_H) return;
  int x0 = gifX + d->iX;
  int w  = d->iWidth;
  if (w > 256) w = 256;
  if (x0 < 0) { src -= x0; w += x0; x0 = 0; }
  if (x0 + w > SCREEN_W) w = SCREEN_W - x0;
  if (w <= 0) return;
  for (int i = 0; i < w; i++) {
    uint8_t idx = src[i];
    spr.drawPixel(x0 + i, y, (hasT && idx == t) ? pal.bg : pal16[idx]);
  }
}

// --- Public ------------------------------------------------------------------

bool characterInit(const char* name) {
  // FS is mounted by setup(); a redundant begin() is safe but not needed.
  // No name -> scan /characters/ for the first directory present, so the
  // boot character is whatever was last installed.
  static char scanned[24];
  if (!name) {
    File d = LittleFS.open("/characters");
    if (d && d.isDirectory()) {
      File e = d.openNextFile();
      while (e) {
        if (e.isDirectory()) {
          const char* n = strrchr(e.name(), '/');
          strncpy(scanned, n ? n + 1 : e.name(), sizeof(scanned) - 1);
          scanned[sizeof(scanned) - 1] = 0;
          name = scanned;
          break;
        }
        e = d.openNextFile();
      }
      d.close();
    }
    if (!name) {
      Serial.println("[char] no characters installed");
      return false;
    }
  }

  mf.colors = pal;   // seed with current palette; manifest overrides per-field
  if (!manifestLoad(name, &mf)) return false;

  snprintf(basePath, sizeof(basePath), "/characters/%s", name);
  pal = mf.colors;
  memset(stateRot, 0, sizeof(stateRot));
  curState = 0xFF;

  if (mf.textMode) {
    loaded = true;
    Serial.printf("[char] loaded '%s' (text mode)\n", mf.name);
    return true;
  }

  gif.begin(LITTLE_ENDIAN_PIXELS);
  loaded = true;
  Serial.printf("[char] loaded '%s' from %s (%u gifs)\n", mf.name, basePath, mf.gifTotal);
  return true;
}

bool characterLoaded() { return loaded; }
const Palette& characterPalette() { return pal; }

void characterClose() {
  if (gifOpen) { gif.close(); gifOpen = false; }
  loaded = false;
  curState = 0xFF;
}

void characterInvalidate() {
  if (!loaded) return;
  if (mf.textMode) {
    spr.fillSprite(pal.bg);
    uint8_t s = curState; curState = 0xFF;
    characterSetState(s);
    return;
  }
  if (gifOpen) { gif.close(); gifOpen = false; }
  animPauseUntil = 0;
  uint8_t s = curState; curState = 0xFF;
  characterSetState(s);
}

void characterSetState(uint8_t s) {
  if (!loaded || s >= MANIFEST_N_STATES || s == curState) return;

  if (mf.textMode) {
    curState = s;
    textFrame = 0;
    textNext = 0;
    spr.fillSprite(pal.bg);
    return;
  }

  if (gifOpen) { gif.close(); gifOpen = false; }
  animPauseUntil = 0;
  curState = s;

  if (mf.stateCount[s] == 0) {
    Serial.printf("[char] no gif for state %d\n", s);
    return;
  }

  uint8_t idx = mf.stateStart[s] + stateRot[s];
  char full[80];
  snprintf(full, sizeof(full), "%s/%s", basePath, mf.gifPaths[idx]);
  if (gif.open(full, gifOpenCb, gifCloseCb, gifReadCb, gifSeekCb, gifDrawCb)) {
    gifOpen = true;
    gifW = gif.getCanvasWidth();
    gifH = gif.getCanvasHeight();
    gifPlace();
    spr.fillSprite(pal.bg);
    nextFrameAt = 0;
    variantStartedMs = millis();
    Serial.printf("[char] %s: %dx%d @ (%d,%d) heap=%u\n",
                  mf.gifPaths[idx], gifW, gifH, gifX, gifY,
                  (unsigned)ESP.getFreeHeap());
  } else {
    Serial.printf("[char] open failed: %s (err %d)\n", full, gif.getLastError());
  }
}

void characterTick() {
  if (!loaded) return;

  if (mf.textMode) {
    if (curState >= MANIFEST_N_STATES) return;
    TextState& ts = mf.textStates[curState];
    if (ts.nFrames == 0) return;
    uint32_t now = millis();
    if (now < textNext) return;
    textNext = now + ts.delayMs;

    // Clear a band around the text, not the whole sprite — the status
    // strip and HUD live below and repaint themselves.
    int cy = 60;
    spr.fillRect(0, cy - 14, SCREEN_W, 28, pal.bg);

    const char* line = ts.frames[textFrame];
    int tw = (int)strlen(line) * 12;   // size-2 glyph width
    spr.setTextColor(pal.body, pal.bg);
    spr.setTextSize(2);
    spr.setCursor((SCREEN_W - tw) / 2, cy - 8);
    spr.print(line);
    spr.setTextSize(1);

    textFrame = (textFrame + 1) % ts.nFrames;
    return;
  }

  uint32_t now = millis();

  if (!gifOpen) {
    // Between animations in a rotation: hold the last frame, then open the
    // next gif when the pause elapses.
    if (animPauseUntil && now >= animPauseUntil) {
      animPauseUntil = 0;
      uint8_t s = curState; curState = 0xFF;
      characterSetState(s);
    }
    return;
  }
  if (now < nextFrameAt) return;

  int delayMs = 0;
  if (!gif.playFrame(false, &delayMs)) {
    // End of animation. Single-gif states freeze on the last frame instead
    // of reopening — the LittleFS open + header decode is a blocking
    // multi-ms burst that upstream saw starving the BT controller.
    if (mf.stateCount[curState] == 1) {
      gif.close();
      gifOpen = false;
      return;
    }
    // Multi-variant: loop the same GIF until the dwell window elapses,
    // then rotate. Short idle loops get several plays instead of one flash.
    if (now - variantStartedMs < VARIANT_DWELL_MS) {
      gif.reset();
      nextFrameAt = now;
      return;
    }
    gif.close(); gifOpen = false;
    stateRot[curState] = (stateRot[curState] + 1) % mf.stateCount[curState];
    animPauseUntil = now + ANIM_PAUSE_MS;
    return;
  }
  nextFrameAt = now + (delayMs > 0 ? delayMs : 100);
}
