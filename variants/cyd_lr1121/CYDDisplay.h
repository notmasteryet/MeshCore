#pragma once

#include <helpers/ui/LGFXDisplay.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#ifndef CYD_BRIGHTNESS
  #define CYD_BRIGHTNESS 160
#endif

#ifndef CYD_ROTATION
  #define CYD_ROTATION 3   // portrait upright (3 + panel offset 1 = 0), as in the Meshtastic port
#endif

// 2.8" 240x320 panel driven as ST7789 + XPT2046 resistive touch, both on HSPI.
class CYD_LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_SPI      _bus;
  lgfx::Light_PWM    _light;
  lgfx::Touch_XPT2046 _touch;

public:
  CYD_LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host   = HSPI_HOST;
      cfg.spi_mode   = 0;
      cfg.freq_write = TFT_SPI_FREQ;
      cfg.freq_read  = 16000000;
      cfg.spi_3wire  = false;
      cfg.use_lock   = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = PIN_TFT_SCK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = PIN_TFT_MISO;
      cfg.pin_dc   = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs   = PIN_TFT_CS;
      cfg.pin_rst  = -1;
      cfg.pin_busy = -1;
      cfg.panel_width  = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 1;
      cfg.dummy_read_pixel = 8;
      cfg.readable  = true;
      cfg.invert    = false;
      cfg.rgb_order = false;  // RGB (true swapped red/blue: the accent showed amber)
      cfg.dlen_16bit = false;
      cfg.bus_shared = true;  // touch shares this SPI bus
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = PIN_TFT_BL;
      cfg.invert = false;
      cfg.freq   = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      cfg.x_min = 434;  cfg.x_max = 3755;
      cfg.y_min = 390;  cfg.y_max = 3818;
      cfg.pin_int = -1;
      cfg.bus_shared = true;
      cfg.offset_rotation = 3;
      cfg.spi_host = HSPI_HOST;
      cfg.freq     = 2500000;
      cfg.pin_sclk = PIN_TFT_SCK;
      cfg.pin_mosi = PIN_TFT_MOSI;
      cfg.pin_miso = PIN_TFT_MISO;
      cfg.pin_cs   = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

class CYDDisplay : public LGFXDisplay {
  CYD_LGFX disp;
public:
  // Odd rotations (1, 3) are portrait on this panel because of its offset_rotation of 1.
  CYDDisplay() : LGFXDisplay((CYD_ROTATION & 1) ? 240 : 320, (CYD_ROTATION & 1) ? 320 : 240, disp) {}

  // LGFXDisplay::begin() puts its frame sprite in PSRAM; this ESP32 has none,
  // so allocate it from internal RAM (8-bit, width()/height() are already zoomed).
  bool begin() {
    display->init();   // must precede turnOn(): wakeup() drives the backlight PWM
    turnOn();
    // LovyanGFX adds the panel offset_rotation (1): 3 -> 0 portrait upright, 1 -> 2 portrait flipped, 0/2 -> landscape.
    // (Rotation 1 gave 2 = portrait upside down, letterboxed.)
    display->setRotation(CYD_ROTATION);
    display->setBrightness(CYD_BRIGHTNESS);
    display->setColorDepth(16);
#ifdef CYD_LVGL
    display->fillScreen(0x0000);
    return true;   // LVGL draws straight to the panel; no frame sprite
#else
    buffer.setColorDepth(8);
    buffer.setPsram(false);
    return buffer.createSprite(width(), height()) != nullptr;
#endif
  }

  lgfx::LGFX_Device* gfx() { return display; }

#ifdef CYD_LVGL
  // main.cpp draws "Loading..." through DisplayDriver before the UI starts; LVGL owns the panel.
  void startFrame(ColorVal bkg = UIColor::window_bkg) override { }
  void setTextSize(int sz) override { }
  void setColor(ColorVal c) override { }
  void setCursor(int x, int y) override { }
  void print(const char* str) override { }
  void fillRect(int x, int y, int w, int h) override { }
  void drawRect(int x, int y, int w, int h) override { }
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override { }
  uint16_t getTextWidth(const char* str) override { return 0; }
  void endFrame() override { }
#endif
};
