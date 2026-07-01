#pragma once
#include <stdint.h>

// BOOT button (GPIO9, the only user button): short press and long press
// (>=800ms) are the whole input vocabulary.
//   prompt pending: short = approve ("once"), long = deny
//   otherwise:      short = next pet,        long = demo mode toggle

enum InputEvent : uint8_t { INPUT_NONE, INPUT_SHORT, INPUT_LONG };

void inputInit();
InputEvent inputPoll();

// Test seam: inject a synthetic event (driven by "!short"/"!long" debug
// lines on USB serial — not part of the BLE protocol).
void inputInject(InputEvent ev);
