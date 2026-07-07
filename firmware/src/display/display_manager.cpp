#include "display_manager.h"
#include "../config.h"
#include <TFT_eSPI.h>
#include <lvgl.h>

// ── Static objects ──────────────────────────────────────────
static TFT_eSPI tft = TFT_eSPI(DISPLAY_WIDTH, DISPLAY_HEIGHT);

// LVGL display buffer — internal DMA-capable SRAM (WROOM-32 has no PSRAM).
// 30 rows × 2 buffers = ~28.8KB, leaves headroom for the BT Classic stack.
#define LV_BUF_SIZE (DISPLAY_WIDTH * 30)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
static lv_color_t *buf2 = nullptr;
static lv_disp_drv_t disp_drv;

// ── LVGL flush callback ─────────────────────────────────────
static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    // LV_COLOR_16_SWAP=1 already produces SPI byte order — no second swap
    tft.pushColors((uint16_t *)&color_p->full, w * h, false);
    tft.endWrite();

    lv_disp_flush_ready(drv);
}

// ── Public API ──────────────────────────────────────────────

void Display::init() {
    // Backlight
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);

    // TFT
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // LVGL core init
    lv_init();

    // Allocate draw buffers in internal DMA-capable RAM
    buf1 = (lv_color_t *)heap_caps_malloc(LV_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    buf2 = (lv_color_t *)heap_caps_malloc(LV_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);

    if (!buf1) {
        // Heap exhausted — fall back to a small static single buffer
        static lv_color_t fb1[DISPLAY_WIDTH * 16];
        buf1 = fb1;
        if (buf2) { heap_caps_free(buf2); buf2 = nullptr; }
        lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, DISPLAY_WIDTH * 16);
    } else {
        // buf2 may be nullptr → single buffering, still fine
        lv_disp_draw_buf_init(&draw_buf, buf1, buf2, LV_BUF_SIZE);
    }

    // Display driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    Serial.println("[Display] TFT + LVGL initialized");
}

void Display::update() {
    lv_timer_handler();
}

void Display::setBacklight(uint8_t brightness) {
    analogWrite(PIN_TFT_BL, brightness);
}
