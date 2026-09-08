#include "display_init.h"

#include <LGFX_CYD.hpp>
#include <lvgl.h>

static LGFX lcd;

static constexpr uint16_t SCREEN_W = 320;
static constexpr uint16_t SCREEN_H = 240;
static constexpr uint16_t DRAW_BUF_LINES = 20;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf1[SCREEN_W * DRAW_BUF_LINES];
static lv_color_t buf2[SCREEN_W * DRAW_BUF_LINES];

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;

  lcd.startWrite();
  lcd.setAddrWindow(area->x1, area->y1, w, h);
  lcd.pushPixelsDMA(reinterpret_cast<uint16_t *>(color_p), w * h);
  lcd.endWrite();

  lv_disp_flush_ready(drv);
}

static void touchpad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
  int32_t x, y;
  if (lcd.getTouch(&x, &y)) {
    data->state = LV_INDEV_STATE_PR;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_REL;
  }
}

void display_init() {
  lcd.init();
  lcd.setRotation(1);
  lcd.setBrightness(255);
  lcd.setSwapBytes(true);  // LVGL emits RGB565 in the byte order ILI9341 expects when swapped

  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, buf2, SCREEN_W * DRAW_BUF_LINES);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_W;
  disp_drv.ver_res = SCREEN_H;
  disp_drv.flush_cb = disp_flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touchpad_read_cb;
  lv_indev_drv_register(&indev_drv);
}
