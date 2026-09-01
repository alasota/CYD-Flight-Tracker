// LovyanGFX device config for the ESP32-2432S028R ("CYD") — classic
// ILI9341 + resistive-touch "R" board. Pinout per CLAUDE.md / hardware
// section. Kept as its own header (rather than build_flags, as with
// TFT_eSPI) because LovyanGFX is configured through a C++ LGFX_Device
// subclass, not preprocessor defines.
//
// Touch (XPT2046) is intentionally NOT wired up here — it stays on the
// separate XPT2046_Touchscreen library on its own SPI bus, per CLAUDE.md.
//
// The display bus below is deliberately pinned to HSPI_HOST, not VSPI_HOST
// — XPT2046_Touchscreen::begin() (see touch_input.cpp) hardcodes use of
// the Arduino core's global `SPI` object, which is itself hardcoded to
// VSPI on classic ESP32 (see SPI.cpp: `SPIClass SPI(VSPI);`). Putting the
// display on VSPI too would silently fight the touch controller for the
// same physical SPI peripheral (different pins, same hardware unit) —
// symptom: touch input flaky or dead, e.g. the view-toggle button not
// responding. Both peripherals are free to use arbitrary GPIOs via the
// ESP32's GPIO matrix regardless of which SPI unit they're bound to, so
// this doesn't require changing any pin numbers below.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;

 public:
  LGFX(void) {
    {  // Display SPI bus
      auto cfg = _bus_instance.config();
      cfg.spi_host = HSPI_HOST;  // VSPI is claimed by touch's global SPI object — see note above
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

    setPanel(&_panel_instance);
  }
};
