#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

// Sunton ESP32-2432S028R ("Cheap Yellow Display") — ILI9341 + XPT2046.
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;
  lgfx::Touch_XPT2046 _touch;

public:
  LGFX() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = VSPI_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = false;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk = 14;
      cfg.pin_mosi = 13;
      cfg.pin_miso = 12;
      cfg.pin_dc = 2;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {
      auto cfg = _panel.config();
      cfg.pin_cs = 15;
      cfg.pin_rst = -1;   // tied to EN, not separately wired
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.invert = false;
      cfg.rgb_order = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {
      auto cfg = _touch.config();
      // x_min/x_max swapped (not 0/4095) -- measured live: tapping the
      // left edge of the screen (the "Weather" tab) was registering on the
      // right edge ("Tab 3"), while Y tracked correctly. That's this
      // panel's raw touch X axis running opposite to the display's X axis;
      // swapping which raw ADC value maps to which panel edge corrects it
      // without touching rotation (which would also affect Y, and Y was
      // already right).
      cfg.x_min = 4095;
      cfg.x_max = 0;
      cfg.y_min = 0;
      cfg.y_max = 4095;
      cfg.pin_int = 36;
      cfg.bus_shared = false;   // separate SPI bus from the display
      cfg.offset_rotation = 0;
      cfg.spi_host = HSPI_HOST;
      cfg.freq = 1000000;
      cfg.pin_sclk = 25;
      cfg.pin_mosi = 32;
      cfg.pin_miso = 39;
      cfg.pin_cs = 33;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};
