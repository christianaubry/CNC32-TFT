// Display.cpp — pilote LovyanGFX (ST7796 + XPT2046) connecté à LVGL 8.3.
#include "Display.h"

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Arduino.h>
#include <Preferences.h>
#include "board_config.h"
#include "../splash_screen.h"

// --- Périphérique LovyanGFX spécifique à la carte maker.fr V2 ---------------
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7796   _panel;
  lgfx::Bus_SPI        _bus;
  lgfx::Light_PWM      _light;
  lgfx::Touch_XPT2046  _touch;

 public:
  LGFX() {
    {  // Bus SPI de l'écran (HSPI)
      auto cfg = _bus.config();
      cfg.spi_host    = TFT_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = TFT_SPI_FREQ;
      cfg.freq_read   = 16000000;
      cfg.spi_3wire   = false;
      cfg.use_lock    = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = PIN_TFT_SCLK;
      cfg.pin_mosi    = PIN_TFT_MOSI;
      cfg.pin_miso    = PIN_TFT_MISO;
      cfg.pin_dc      = PIN_TFT_DC;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    {  // Panneau ST7796
      auto cfg = _panel.config();
      cfg.pin_cs          = PIN_TFT_CS;
      cfg.pin_rst         = PIN_TFT_RST;
      cfg.pin_busy        = -1;
      cfg.panel_width     = TFT_PANEL_W;
      cfg.panel_height    = TFT_PANEL_H;
      cfg.offset_x        = 0;
      cfg.offset_y        = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable        = true;
      cfg.invert          = false;
      cfg.rgb_order       = false;
      cfg.dlen_16bit      = false;
      cfg.bus_shared      = false;  // bus dédié à l'écran
      _panel.config(cfg);
    }
    {  // Rétroéclairage PWM
      auto cfg = _light.config();
      cfg.pin_bl      = PIN_TFT_BL;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 7;
      _light.config(cfg);
      _panel.setLight(&_light);
    }
    {  // Tactile XPT2046 sur bus VSPI séparé
      auto cfg = _touch.config();
      cfg.x_min      = TOUCH_RAW_XMIN;
      cfg.x_max      = TOUCH_RAW_XMAX;
      cfg.y_min      = TOUCH_RAW_YMIN;
      cfg.y_max      = TOUCH_RAW_YMAX;
      cfg.pin_int    = PIN_TOUCH_IRQ;
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;
      cfg.spi_host   = TOUCH_HOST;
      cfg.freq       = TOUCH_SPI_FREQ;
      cfg.pin_sclk   = PIN_TOUCH_SCLK;
      cfg.pin_mosi   = PIN_TOUCH_MOSI;
      cfg.pin_miso   = PIN_TOUCH_MISO;
      cfg.pin_cs     = PIN_TOUCH_CS;
      _touch.config(cfg);
      _panel.setTouch(&_touch);
    }
    setPanel(&_panel);
  }
};

static LGFX lcd;

// --- Tampon de rendu LVGL (40 lignes, simple tampon) ------------------------
static constexpr uint32_t BUF_LINES = 40;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * BUF_LINES];

static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* px) {
  const uint32_t w = area->x2 - area->x1 + 1;
  const uint32_t h = area->y2 - area->y1 + 1;
  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.pushPixelsDMA(reinterpret_cast<uint16_t*>(px), w * h);
  lcd.endWrite();
  lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t*, lv_indev_data_t* data) {
  uint16_t x = 0, y = 0;
  if (lcd.getTouch(&x, &y)) {
    data->state   = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void display_init() {
  lv_init();

  lcd.init();
  lcd.setRotation(TFT_ROTATION);

  // Splash screen avant LVGL
  lcd.fillScreen(TFT_BLACK);
  display_set_brightness(220);
  lcd.drawPng(splash_screen_png, splash_screen_png_size, 0, 0);
  delay(3000);
  lcd.fillScreen(TFT_BLACK);

  Preferences prefs;
  prefs.begin("display", true);
  uint16_t cal[8];
  bool needs_cal = false;
  if (prefs.getBytes("touch_cal", cal, sizeof(cal)) == sizeof(cal)) {
    lcd.setTouchCalibrate(cal);
    Serial.println("Touch cal: chargee depuis NVS");
  } else {
    needs_cal = true;
  }
  prefs.end();

  lcd.setSwapBytes(true);          // LVGL pousse du RGB565 octet-inversé

  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, SCREEN_W * BUF_LINES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = SCREEN_W;
  disp_drv.ver_res  = SCREEN_H;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type    = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read;
  lv_indev_drv_register(&indev_drv);

  if (needs_cal) {
    display_calibrate();
  }
}

void display_set_brightness(uint8_t value) {
  lcd.setBrightness(value);
}

void display_calibrate() {
  uint16_t cal[8];
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_WHITE);
  lcd.setCursor(10, 10);
  lcd.print("Touchez les coins...");
  lcd.calibrateTouch(cal, TFT_WHITE, TFT_BLACK, 20);
  Serial.print("Touch cal: ");
  for (int i = 0; i < 8; ++i) { Serial.print(cal[i]); Serial.print(' '); }
  Serial.println();

  Preferences prefs;
  prefs.begin("display", false);
  prefs.putBytes("touch_cal", cal, sizeof(cal));
  prefs.end();
  Serial.println("Touch cal: sauvegardee dans NVS");

  lv_obj_invalidate(lv_scr_act());
}
