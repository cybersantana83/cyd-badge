#pragma once
// Configuracao do display da CYD (ESP32-2432S028) para LovyanGFX.
// Display em HSPI: SCLK 14, MOSI 13, MISO 12, DC 2, CS 15, Backlight 21.
#include <SD.h>  // precisa vir antes do LovyanGFX para habilitar drawJpgFile(SD, ...)
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX_CYD : public lgfx::LGFX_Device {
#if defined(CYD_PANEL_ST7789)
  lgfx::Panel_ST7789 _panel;
#else
  lgfx::Panel_ILI9341 _panel;
#endif
  lgfx::Bus_SPI _bus;
  lgfx::Light_PWM _light;

 public:
  LGFX_CYD() {
    {
      auto cfg = _bus.config();
      cfg.spi_host = HSPI_HOST;
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
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;
      cfg.panel_width = 240;
      cfg.panel_height = 320;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.readable = true;
#if defined(CYD_PANEL_ST7789)
      cfg.invert = true;
#else
      cfg.invert = false;
#endif
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel.config(cfg);
    }
    {
      auto cfg = _light.config();
      cfg.pin_bl = 21;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    setPanel(&_panel);
  }
};
