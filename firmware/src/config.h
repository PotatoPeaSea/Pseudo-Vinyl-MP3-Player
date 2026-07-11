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
#define PIN_TFT_MOSI        4       // SDA
#define PIN_TFT_SCLK        16      // SCL
#define PIN_TFT_CS          2
#define PIN_TFT_DC          21
#define PIN_TFT_RST         15
// Display module has no backlight pin (BL hardwired on-board); brightness
// control is unavailable.

// ── SD Card (HSPI) ──────────────────────────────────────────
#define PIN_SD_SCLK         18
#define PIN_SD_MOSI         17
#define PIN_SD_MISO         19
#define PIN_SD_CS           5

// ── Audio: Bluetooth A2DP only ──────────────────────────────
// Wired I2S/PCM5102 output was removed: on the no-PSRAM WROOM-32 the
// Classic-BT + A2DP stack (~108KB) plus SD/FATFS + LVGL UI leaves no room
// for a second output path. Output is Bluetooth-only.

// ── Rotary Encoder (KY-040) ─────────────────────────────────
#define PIN_ENC_A           32
#define PIN_ENC_B           33
#define PIN_ENC_SW          27

// ── Buttons ─────────────────────────────────────────────────
#define PIN_BTN_PLAY        13      // moved off 16 (now TFT SCLK)
#define PIN_BTN_NEXT        14      // moved off 17 (now SD MOSI)
#define PIN_BTN_PREV        23      // moved off 21 (now TFT DC)

// ── Battery voltage divider (ADC1_CH6 — usable with BT on) ──
#define PIN_BATT_ADC        34

// ── Bluetooth ───────────────────────────────────────────────
#define BT_DEVICE_NAME      "Pseudo Vinyl"
#define BT_RINGBUF_BYTES    (8 * 1024)    // ~46ms of 44.1k stereo PCM (heap-constrained)
// Cap on the discovery list — every entry costs a String on the heap and a
// ~700-byte LVGL button; without a cap a busy radio environment grows it
// unbounded.
#define MAX_BT_DEVICES      8

// ── Audio Defaults ──────────────────────────────────────────
#define DEFAULT_VOLUME      12      // 0-21
#define MAX_VOLUME          21

// ── Library limit ───────────────────────────────────────────
// Hard cap on songs loaded into RAM. Each LVGL list button is ~700 bytes
// (only while the song-list screen is shown); one shared SongInfo vector
// holds the library for the whole app.
#define MAX_SONGS           15

// ── Album Art ───────────────────────────────────────────────
// No PSRAM: 240×240 RGB565 (113KB) doesn't fit next to the BT stack.
// Art files are ≤90×90 RGB565 (16.2KB) — the vinyl label is 90px, so
// anything larger was downscaled at display time anyway and just wasted
// heap. Re-run tools/prescale_art with size 90 for old 120px .art files.
#define ART_MAX_SIDE        90
#define ART_MAX_BYTES       (ART_MAX_SIDE * ART_MAX_SIDE * 2)

// ── UI Constants ────────────────────────────────────────────
#define VINYL_SPIN_SPEED_DEG 2       // Degrees per frame
#define UI_REFRESH_MS        16      // ~60fps LVGL tick
#define INPUT_POLL_MS        5       // Button/encoder poll interval
#define DEBOUNCE_MS          50      // Button debounce
