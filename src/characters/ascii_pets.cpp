#include "ascii_pets.h"
#include <Arduino.h>
#include <string.h>
#include "../display.h"
#include "../stats.h"

// Mirrors PersonaState in state.h
enum { B_SLEEP, B_IDLE, B_BUSY, B_ATTENTION, B_CELEBRATE, B_DIZZY, B_HEART };

// ---- shared geometry ----
// Center of the 172px screen; upstream centered on 67 of 135. Species art
// is position-agnostic (everything routes through these constants).
const int BUDDY_X_CENTER = 86;
const int BUDDY_CANVAS_W = 172;
const int BUDDY_Y_BASE   = 30;
const int BUDDY_Y_OVERLAY = 6;
const int BUDDY_CHAR_W   = 6;
const int BUDDY_CHAR_H   = 8;

// ---- shared colors ----
const uint16_t BUDDY_BG     = 0x0000;
const uint16_t BUDDY_HEART  = 0xF810;
const uint16_t BUDDY_DIM    = 0x8410;
const uint16_t BUDDY_YEL    = 0xFFE0;
const uint16_t BUDDY_WHITE  = 0xFFFF;
const uint16_t BUDDY_CYAN   = 0x07FF;
const uint16_t BUDDY_GREEN  = 0x07E0;
const uint16_t BUDDY_PURPLE = 0xA01F;
const uint16_t BUDDY_RED    = 0xF800;
const uint16_t BUDDY_BLUE   = 0x041F;

// ---- shared rendering helpers ----
// Target indirection kept from upstream (they retargeted for the landscape
// clock; we keep it for flexibility). LGFX_Sprite and LGFX_Device share the
// LovyanGFX base, which carries all the drawing methods used here.
static lgfx::LovyanGFX* _tgt = &spr;
// 2x on the home screen (172px fits 12 glyphs * 12px). Species art is
// space-padded to fixed width; at 2x helpers trim + re-center per line so
// padding doesn't push ink off-screen.
static uint8_t _scale = 2;

void buddyPrintLine(const char* line, int yPx, uint16_t color, int xOff) {
  int len = strlen(line);
  if (_scale > 1) {
    while (len && line[len - 1] == ' ') len--;
    while (len && *line == ' ')         { line++; len--; }
  }
  int w = len * BUDDY_CHAR_W * _scale;
  int x = BUDDY_X_CENTER - w / 2 + xOff * _scale;
  _tgt->setTextColor(color, BUDDY_BG);
  _tgt->setCursor(x, yPx);
  for (int i = 0; i < len; i++) _tgt->print(line[i]);
}

void buddyPrintSprite(const char* const* lines, uint8_t nLines, int yOffset,
                      uint16_t color, int xOff) {
  _tgt->setTextSize(_scale);
  int yBase = BUDDY_Y_BASE * _scale - (_scale - 1) * 14;
  for (uint8_t i = 0; i < nLines; i++) {
    buddyPrintLine(lines[i], yBase + (yOffset + i * BUDDY_CHAR_H) * _scale, color, xOff);
  }
}

// Species pass 1x coords (relative to BUDDY_X_CENTER / BUDDY_Y_OVERLAY);
// transform here so all 18 species files stay scale-agnostic.
void buddySetCursor(int x, int y) {
  _tgt->setCursor(BUDDY_X_CENTER + (x - BUDDY_X_CENTER) * _scale, y * _scale);
}
void buddySetColor(uint16_t fg) { _tgt->setTextColor(fg, BUDDY_BG); }
void buddyPrint(const char* s)  { _tgt->setTextSize(_scale); _tgt->print(s); }

// ---- species registry ----
extern const Species CAPYBARA_SPECIES;
extern const Species DUCK_SPECIES;
extern const Species GOOSE_SPECIES;
extern const Species BLOB_SPECIES;
extern const Species CAT_SPECIES;
extern const Species DRAGON_SPECIES;
extern const Species OCTOPUS_SPECIES;
extern const Species OWL_SPECIES;
extern const Species PENGUIN_SPECIES;
extern const Species TURTLE_SPECIES;
extern const Species SNAIL_SPECIES;
extern const Species GHOST_SPECIES;
extern const Species AXOLOTL_SPECIES;
extern const Species CACTUS_SPECIES;
extern const Species ROBOT_SPECIES;
extern const Species RABBIT_SPECIES;
extern const Species MUSHROOM_SPECIES;
extern const Species CHONK_SPECIES;

static const Species* SPECIES_TABLE[] = {
  &CAPYBARA_SPECIES, &DUCK_SPECIES, &GOOSE_SPECIES, &BLOB_SPECIES,
  &CAT_SPECIES, &DRAGON_SPECIES, &OCTOPUS_SPECIES, &OWL_SPECIES,
  &PENGUIN_SPECIES, &TURTLE_SPECIES, &SNAIL_SPECIES, &GHOST_SPECIES,
  &AXOLOTL_SPECIES, &CACTUS_SPECIES, &ROBOT_SPECIES, &RABBIT_SPECIES,
  &MUSHROOM_SPECIES, &CHONK_SPECIES,
};
static const uint8_t N_SPECIES = sizeof(SPECIES_TABLE) / sizeof(SPECIES_TABLE[0]);
static uint8_t currentSpeciesIdx = 0;

// ---- tick state ----
static uint32_t tickCount  = 0;
static uint32_t nextTickAt = 0;
static const uint32_t TICK_MS = 200;

void buddyInit() {
  tickCount = 0;
  nextTickAt = 0;
  uint8_t saved = speciesIdxLoad();
  if (saved < N_SPECIES) currentSpeciesIdx = saved;
}

void buddySetSpeciesIdx(uint8_t idx) {
  if (idx < N_SPECIES) currentSpeciesIdx = idx;
}

void buddySetSpecies(const char* name) {
  for (uint8_t i = 0; i < N_SPECIES; i++) {
    if (strcmp(SPECIES_TABLE[i]->name, name) == 0) {
      currentSpeciesIdx = i;
      return;
    }
  }
}

const char* buddySpeciesName() { return SPECIES_TABLE[currentSpeciesIdx]->name; }
uint8_t buddySpeciesCount()    { return N_SPECIES; }
uint8_t buddySpeciesIdx()      { return currentSpeciesIdx; }

void buddyNextSpecies() {
  currentSpeciesIdx = (currentSpeciesIdx + 1) % N_SPECIES;
  speciesIdxSave(currentSpeciesIdx);
}

// Only redraw when tickCount changes — animations run at TICK_MS (5 fps),
// the loop runs faster, and the redraw is identical between ticks. State
// changes redraw immediately so transitions appear instantly.
static uint8_t lastDrawnState = 0xFF;
static uint8_t lastDrawnSpecies = 0xFF;
void buddyInvalidate() { lastDrawnState = 0xFF; }

void buddySetPeek(bool peek) {
  uint8_t s = peek ? 1 : 2;
  if (s == _scale) return;
  _scale = s;
  buddyInvalidate();
}

void buddyTick(uint8_t personaState) {
  uint32_t now = millis();
  bool ticked = false;
  if ((int32_t)(now - nextTickAt) >= 0) {
    nextTickAt = now + TICK_MS;
    tickCount++;
    ticked = true;
  }

  if (personaState >= 7) personaState = B_IDLE;
  if (!ticked && personaState == lastDrawnState
              && currentSpeciesIdx == lastDrawnSpecies) {
    return;
  }
  lastDrawnState = personaState;
  lastDrawnSpecies = currentSpeciesIdx;

  // Clear the character region only — status strip + HUD live below it.
  spr.fillRect(0, 0, BUDDY_CANVAS_W, CHAR_REGION_H, BUDDY_BG);

  const Species* sp = SPECIES_TABLE[currentSpeciesIdx];
  if (sp->states[personaState]) sp->states[personaState](tickCount);
}
