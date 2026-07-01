#include "display.h"

BuddyDisplay lcd;
lgfx::LGFX_Sprite spr(&lcd);

BuddyDisplay::BuddyDisplay() {
  {
    auto cfg = _bus.config();
    cfg.spi_host   = SPI2_HOST;   // C6's only general-purpose SPI
    cfg.spi_mode   = 0;
    cfg.freq_write = 40000000;
    cfg.pin_sclk   = 7;
    cfg.pin_mosi   = 6;
    cfg.pin_miso   = -1;
    cfg.pin_dc     = 15;
    cfg.use_lock   = true;
    _bus.config(cfg);
    _panel.setBus(&_bus);
  }
  {
    auto cfg = _panel.config();
    cfg.pin_cs           = 14;
    cfg.pin_rst          = 21;
    cfg.pin_busy         = -1;
    cfg.panel_width      = 172;
    cfg.panel_height     = 320;
    cfg.offset_x         = 34;    // (240-172)/2: glass is centered in controller RAM
    cfg.offset_y         = 0;
    cfg.offset_rotation  = 0;
    cfg.readable         = false;
    cfg.invert           = true;  // IPS panel: inversion on, else colors render negative
    cfg.rgb_order        = false;
    cfg.bus_shared       = true;  // microSD shares MOSI/SCLK
    _panel.config(cfg);
  }
  {
    auto cfg = _light.config();
    cfg.pin_bl      = 22;
    cfg.invert      = false;      // active-high
    cfg.freq        = 12000;
    cfg.pwm_channel = 0;
    _light.config(cfg);
    _panel.setLight(&_light);
  }
  setPanel(&_panel);
}

bool displayInit() {
  lcd.init();
  lcd.setRotation(0);
  lcd.setBrightness(200);
  lcd.fillScreen(TFT_BLACK);
  spr.setColorDepth(16);
  bool ok = spr.createSprite(SCREEN_W, SCREEN_H) != nullptr;
  if (ok) spr.fillSprite(TFT_BLACK);
  return ok;
}
