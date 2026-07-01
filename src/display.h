#pragma once
#include <LovyanGFX.hpp>

// Waveshare ESP32-C6-LCD-1.47: ST7789 240x320 controller driving 172x320
// glass, portrait. The 172 visible columns sit centered in controller RAM,
// hence offset_x = 34. Backlight on GPIO22, active-high, PWM-dimmable.
class BuddyDisplay : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;

public:
  BuddyDisplay();
};

extern BuddyDisplay lcd;
// Full-screen 16-bit sprite; everything renders here and pushes once per
// frame (same architecture as upstream's TFT_eSprite spr).
extern lgfx::LGFX_Sprite spr;

// Init panel + backlight, allocate the sprite. Returns false if the sprite
// allocation failed (fall back handled by caller).
bool displayInit();

constexpr int SCREEN_W = 172;
constexpr int SCREEN_H = 320;
