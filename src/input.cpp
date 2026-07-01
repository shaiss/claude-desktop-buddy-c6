#include "input.h"
#include <Arduino.h>

// The core's C6 variant header also names this BOOT_PIN (and agrees: GPIO9);
// different name here to avoid the conflicting declaration.
static const int BTN_BOOT = 9;          // pressed = LOW, internal pull-up
static const uint32_t LONG_MS = 800;
static const uint32_t DEBOUNCE_MS = 30;

static bool     pressed = false;
static bool     longFired = false;
static uint32_t pressStartMs = 0;
static uint32_t lastEdgeMs = 0;
static InputEvent injected = INPUT_NONE;

void inputInit() {
  pinMode(BTN_BOOT, INPUT_PULLUP);
}

void inputInject(InputEvent ev) { injected = ev; }

InputEvent inputPoll() {
  if (injected != INPUT_NONE) {
    InputEvent ev = injected;
    injected = INPUT_NONE;
    return ev;
  }

  uint32_t now = millis();
  bool down = digitalRead(BTN_BOOT) == LOW;

  if (down != pressed && (now - lastEdgeMs) >= DEBOUNCE_MS) {
    lastEdgeMs = now;
    pressed = down;
    if (down) {
      pressStartMs = now;
      longFired = false;
    } else if (!longFired) {
      return INPUT_SHORT;              // released before the long threshold
    }
  }

  // Long fires while still held (matches upstream's pressedFor pattern) so
  // the user gets feedback without waiting for release; the release is
  // swallowed via longFired.
  if (pressed && !longFired && (now - pressStartMs) >= LONG_MS) {
    longFired = true;
    return INPUT_LONG;
  }

  return INPUT_NONE;
}
