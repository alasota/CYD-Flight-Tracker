// LovyanGFX device config for the ESP32-2432S028R ("CYD") — classic
// ILI9341 + resistive-touch "R" board. Pinout per CLAUDE.md / hardware
// section. Kept as its own header (rather than build_flags, as with
// TFT_eSPI) because LovyanGFX is configured through a C++ LGFX_Device
// subclass, not preprocessor defines.
//
// Touch (XPT2046) is handled by LovyanGFX itself (lgfx::Touch_XPT2046) —
// no separate touch library. On the CYD the touch controller sits on its
// own SPI wiring (CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36), so it gets its
// own SPI host: the display uses HSPI, touch uses VSPI, and the two never
// share a peripheral (bus_shared = false). Calibration lives in
// touch_calibration.h. Read it with tft.getTouch() / tft.getTouchRaw();
// coordinates already follow tft.setRotation().
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#include "touch_calibration.h"

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;
  lgfx::Touch_XPT2046 _touch_instance;

 public:
  LGFX(void) {
    {  // Display SPI bus
      auto cfg = _bus_instance.config();
      cfg.spi_host = HSPI_HOST;  // touch has VSPI to itself — see note above
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;  // TFT_SCLK
      cfg.pin_mosi = 13;  // TFT_MOSI
      cfg.pin_miso = 12;  // TFT_MISO
      cfg.pin_dc = 2;     // TFT_DC
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {  // Panel
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 15;   // TFT_CS
      cfg.pin_rst = -1;  // TFT_RST: software reset only, no reset pin
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = false;
      cfg.invert = false;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    {  // Backlight
      auto cfg = _light_instance.config();
      cfg.pin_bl = 21;  // TFT_BL
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    {  // Touch: XPT2046 on its own SPI bus (see note at the top)
      auto cfg = _touch_instance.config();
      cfg.x_min = kTouchRawXMin;
      cfg.x_max = kTouchRawXMax;
      cfg.y_min = kTouchRawYMin;
      cfg.y_max = kTouchRawYMax;
      cfg.offset_rotation = kTouchOffsetRotation;
      cfg.pin_int = 36;        // Touch IRQ
      cfg.bus_shared = false;  // separate wiring from the display
      cfg.spi_host = VSPI_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;  // Touch CLK
      cfg.pin_mosi = 32;  // Touch MOSI
      cfg.pin_miso = 39;  // Touch MISO
      cfg.pin_cs = 33;    // Touch CS
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};
