#include "display_manager.h"
#include "../config.h"
#include <TFT_eSPI.h>
#include <lvgl.h>

// ── Static objects ──────────────────────────────────────────
static TFT_eSPI tft = TFT_eSPI(DISPLAY_WIDTH, DISPLAY_HEIGHT);

// LVGL display buffer — internal DMA-capable SRAM. This MUST stay internal
// even though the WROVER has PSRAM: the ESP32's SPI DMA engine cannot read
// from external RAM, so a PSRAM draw buffer would flush garbage. The
// MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL request below is load-bearing, not
// leftover WROOM-era caution.
//
// 80 rows = 38.4KB, single buffer. Was 10 rows (4.8KB) from the no-PSRAM
// ram-squeeze pass — fine when only a small art square redrew, but the
// Now Playing vinyl now covers ~85% of the screen and spins every frame,
// so nearly the whole display invalidates each frame. disp_flush_cb below
// is a blocking SPI write (no async DMA), so LVGL renders+flushes one
// buffer-height strip at a time, waiting for each; at 10 rows that's 24
// sequential strips per frame, which is exactly the visible top-to-bottom
// "wipe" — the screen genuinely is drawn in visible horizontal bands, not
// updated atomically. 80 rows drops that to 3 strips/frame. (Double
// buffering wouldn't help here without an async/DMA-driven flush_cb: LVGL
// only starts the next strip after lv_disp_flush_ready fires, so a second
// buffer just costs RAM with nothing to render into it concurrently — all
// the available headroom went into one bigger buffer instead.)
// Display::init() runs first at boot (~110KB largest contiguous internal
// block before BT/SD claim anything), so 38.4KB here leaves ample margin;
// watch the boot heap log if BT/SD headroom ever gets reintroduced as risky.
#define LV_BUF_ROWS 80
#define LV_BUF_SIZE (DISPLAY_WIDTH * LV_BUF_ROWS)
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf1 = nullptr;
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
    // No backlight pin on this display module — it's always on.

    // TFT
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    // LVGL core init
    lv_init();

    // Allocate the single draw buffer in internal DMA-capable RAM.
    // Display::init is the very first init at boot (~250KB free), so this
    // cannot realistically fail — the old static fallback buffer burned
    // 7.7KB of .bss permanently just in case, which we no longer pay.
    size_t bufPixels = LV_BUF_SIZE;
    buf1 = (lv_color_t *)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!buf1) {
        // Last resort: 4 rows (1.9KB)
        bufPixels = DISPLAY_WIDTH * 4;
        buf1 = (lv_color_t *)heap_caps_malloc(bufPixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (!buf1) {
        // Cannot happen at boot start; if it does, the device is unusable —
        // halt loudly rather than crash somewhere confusing later
        for (;;) {
            Serial.println("[Display] FATAL: no RAM for LVGL draw buffer");
            delay(5000);
        }
    }
    lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, bufPixels);

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
    // No backlight control pin on this display module — no-op.
    (void)brightness;
}
