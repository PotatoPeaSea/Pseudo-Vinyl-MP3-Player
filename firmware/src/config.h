#pragma once
/**
 * Pseudo Vinyl MP3 Player — Hardware Configuration
 * Board: ESP32-WROOM-32 (classic ESP32, 4MB flash, no PSRAM)
 *
 * Pin rules for this chip:
 *  - GPIO 6-11 are flash — never use
 *  - GPIO 0 is a strapping pin — avoid
 *  - GPIO 12 (MTDI) strapping: a pull-up at boot bricks flash voltage —
 *    left unused because many SD modules have pull-ups on all lines
 *  - GPIO 34/35/36/39 are INPUT-ONLY, no internal pull-ups
 *  - ADC2 pins can't do analogRead while Bluetooth is on → battery on ADC1
 */

// ── Feature Flags ───────────────────────────────────────────
#ifndef BT_ENABLED
#define BT_ENABLED          1
#endif

// Debug run mode: serial console simulates buttons/encoder (no hardware
// needed). Enable by building the esp32dev-debug environment.
#ifndef DEBUG_MODE
#define DEBUG_MODE          0
#endif

// ── Display: GC9A01 (240×240 round IPS, VSPI) ───────────────
// NOTE: pins are duplicated in platformio.ini TFT_eSPI build flags —
// keep both in sync!
#define DISPLAY_WIDTH       240
#define DISPLAY_HEIGHT      240
#define PIN_TFT_MOSI        23
#define PIN_TFT_SCLK        18
#define PIN_TFT_CS          5
#define PIN_TFT_DC          2
#define PIN_TFT_RST         4
#define PIN_TFT_BL          19

// ── SD Card (HSPI) ──────────────────────────────────────────
// MISO on input-only GPIO 35 (SD module drives it; no pull-up needed here)
#define PIN_SD_SCLK         14
#define PIN_SD_MOSI         13
#define PIN_SD_MISO         35
#define PIN_SD_CS           15

// ── Audio: PCM5102 DAC (I2S, wired output) ──────────────────
#define PIN_I2S_BCLK        26
#define PIN_I2S_LRCK        25
#define PIN_I2S_DOUT        22

// ── Rotary Encoder (KY-040) ─────────────────────────────────
#define PIN_ENC_A           32
#define PIN_ENC_B           33
#define PIN_ENC_SW          27

// ── Buttons ─────────────────────────────────────────────────
#define PIN_BTN_PLAY        16
#define PIN_BTN_NEXT        17
#define PIN_BTN_PREV        21

// ── Battery voltage divider (ADC1_CH6 — usable with BT on) ──
#define PIN_BATT_ADC        34

// ── Bluetooth ───────────────────────────────────────────────
#define BT_DEVICE_NAME      "Pseudo Vinyl"
#define BT_RINGBUF_BYTES    (16 * 1024)   // ~92ms of 44.1k stereo PCM

// ── Audio Defaults ──────────────────────────────────────────
#define DEFAULT_VOLUME      12      // 0-21
#define MAX_VOLUME          21

// ── Album Art ───────────────────────────────────────────────
// No PSRAM: 240×240 RGB565 (113KB) doesn't fit next to the BT stack.
// Art files are ≤120×120 RGB565 (28.8KB), scaled onto the 90px vinyl label.
#define ART_MAX_SIDE        120
#define ART_MAX_BYTES       (ART_MAX_SIDE * ART_MAX_SIDE * 2)

// ── UI Constants ────────────────────────────────────────────
#define VINYL_SPIN_SPEED_DEG 2       // Degrees per frame
#define UI_REFRESH_MS        16      // ~60fps LVGL tick
#define INPUT_POLL_MS        5       // Button/encoder poll interval
#define DEBOUNCE_MS          50      // Button debounce
