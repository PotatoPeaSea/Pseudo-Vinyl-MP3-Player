#pragma once
/**
 * Pseudo Vinyl MP3 Player — Hardware Configuration
 * Board: ESP32-S3-WROOM-1 N8R8
 *
 * NOTE: GPIO 26-37 are NOT available on this variant
 *       (used by Octal SPI Flash/PSRAM)
 */

// ── Feature Flags ───────────────────────────────────────────
#ifndef BT_ENABLED
#define BT_ENABLED          0       // Set to 1 when Bluetooth is added
#endif

// ── Display: GC9A01 (240×240 round IPS, SPI2/FSPI) ─────────
#define DISPLAY_WIDTH       240
#define DISPLAY_HEIGHT      240
#define PIN_TFT_MOSI        11
#define PIN_TFT_SCLK        12
#define PIN_TFT_CS          10
#define PIN_TFT_DC          13
#define PIN_TFT_RST         14
#define PIN_TFT_BL          21

// ── SD Card (SPI3) ──────────────────────────────────────────
#define PIN_SD_MOSI          15
#define PIN_SD_MISO          16
#define PIN_SD_SCLK          17
#define PIN_SD_CS            18

// ── Audio: PCM5102 DAC (I2S) ────────────────────────────────
#define PIN_I2S_BCLK         4
#define PIN_I2S_LRCK         5
#define PIN_I2S_DOUT         6

// ── Rotary Encoder (KY-040) ─────────────────────────────────
#define PIN_ENC_A            1
#define PIN_ENC_B            2
#define PIN_ENC_SW           42

// ── Buttons ─────────────────────────────────────────────────
#define PIN_BTN_PLAY         40
#define PIN_BTN_NEXT         41
#define PIN_BTN_PREV         39

// ── Audio Defaults ──────────────────────────────────────────
#define DEFAULT_VOLUME       12      // 0-21 for ESP32-audioI2S
#define MAX_VOLUME           21

// ── UI Constants ────────────────────────────────────────────
#define VINYL_SPIN_SPEED_DEG 2       // Degrees per frame
#define UI_REFRESH_MS        16      // ~60fps LVGL tick
#define INPUT_POLL_MS        5       // Button/encoder poll interval
#define DEBOUNCE_MS          50      // Button debounce
