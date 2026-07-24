#pragma once
/**
 * Pseudo Vinyl MP3 Player — Hardware Configuration
 * Board: ESP32-WROVER N4R8 (classic ESP32, 4MB flash, 8MB PSRAM)
 *
 * Pin rules for this module:
 *  - GPIO 6-11 are flash — never use
 *  - GPIO 16/17 are the PSRAM CS/CLK lines on WROVER — never use.
 *    (This is why the display SCLK and SD MOSI moved off them.)
 *  - GPIO 12 (MTDI) strapping: a pull-up at boot bricks flash voltage —
 *    left unused because many SD modules have pull-ups on all lines
 *  - GPIO 34/35/36/39 are INPUT-ONLY, no internal pull-ups
 *  - ADC2 pins can't do analogRead while Bluetooth is on → battery on ADC1
 *  - Strapping pins in use: 0 (TFT MOSI), 2 (TFT CS), 5 (TFT SCLK),
 *    15 (TFT RST) — see the display block for the boot-time caveat
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

// ── Display: GC9A01 (240×240 round IPS) ─────────────────────
// NOTE: pins are duplicated in platformio.ini TFT_eSPI build flags —
// keep both in sync!
//
// CAUTION — GPIO 0 (SDA/MOSI) is the boot-mode strapping pin. It must read
// HIGH at reset or the chip drops into serial download instead of running
// the app. It is safe as an SPI output once booted, but if the panel's SDA
// line has a pull-down (or the module drives it low at reset) the board
// will not boot. If boots become unreliable, this pin is the first suspect:
// add a 10k pull-up to 3V3 on GPIO 0, or move SDA to GPIO 25/26.
#define DISPLAY_WIDTH       240
#define DISPLAY_HEIGHT      240
#define PIN_TFT_MOSI        0       // SDA — strapping pin, see caution above
#define PIN_TFT_SCLK        5       // SCL
#define PIN_TFT_CS          2
#define PIN_TFT_DC          23
#define PIN_TFT_RST         15
// Display module has no backlight pin (BL hardwired on-board); brightness
// control is unavailable.

// ── SD Card (HSPI) ──────────────────────────────────────────
#define PIN_SD_SCLK         21
#define PIN_SD_MOSI         18
#define PIN_SD_MISO         22
#define PIN_SD_CS           19

// ── Audio: Bluetooth A2DP only ──────────────────────────────
// Wired I2S/PCM5102 output was removed back on the no-PSRAM WROOM-32: the
// Classic-BT + A2DP stack (~108KB) plus SD/FATFS + LVGL UI left no room for
// a second output path. Output is Bluetooth-only. The WROVER's PSRAM would
// now make a second path affordable, but re-adding it is out of scope here
// and the pins were reclaimed for the rewired display/SD.

// ── Rotary Encoder (KY-040) ─────────────────────────────────
#define PIN_ENC_A           32
#define PIN_ENC_B           33
#define PIN_ENC_SW          27

// ── Buttons ─────────────────────────────────────────────────
#define PIN_BTN_PLAY        13
#define PIN_BTN_NEXT        14
#define PIN_BTN_PREV        4       // moved off 23 (now TFT DC); 4 was freed
                                    // when TFT MOSI moved to GPIO 0

// ── Battery voltage divider (ADC1_CH6 — usable with BT on) ──
#define PIN_BATT_ADC        34

// ── Bluetooth ───────────────────────────────────────────────
#define BT_DEVICE_NAME      "Pseudo Vinyl"
// ~58ms of 44.1k stereo PCM. 8KB (46ms) stuttered on hardware; 16KB starved
// the Bluedroid media path (+ resident decoder) and killed the stream
// entirely. 10KB is the compromise — underrun telemetry in the data
// callback measures whether it is enough. Allocated once at boot.
#define BT_RINGBUF_BYTES    (10 * 1024)
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

// Playlists = top-level SD folders, plus a synthesized "All Songs" (root)
// entry. Cheap in RAM (two Strings per entry, no song data), capped mainly
// so the Playlists screen's LVGL button list stays bounded like Song List
// and Bluetooth already are.
#define MAX_PLAYLISTS       8

// ── Album Art ───────────────────────────────────────────────
// 240×240 RGB565 (112.5KB) — the display's native resolution, and the
// largest size the pre-scaler tool has ever produced, so no .art file is
// ever rejected on size grounds. The vinyl label still renders at 90px
// (lv_img_set_zoom downscales at display time), so this cap is purely
// about accepting whatever the tool wrote, not a rendering size. Raised
// from 90 on the no-PSRAM WROOM-32: that board's 240×240 buffer (113KB)
// couldn't fit next to the BT stack, but the WROVER's PSRAM absorbs
// allocations this size automatically (anything >4KB lands in PSRAM, see
// docs/MEMORY.md "Board change: WROOM-32 → WROVER N4R8").
#define ART_MAX_SIDE        240
#define ART_MAX_BYTES       (ART_MAX_SIDE * ART_MAX_SIDE * 2)

// ── UI Constants ────────────────────────────────────────────
#define VINYL_SPIN_SPEED_DEG 2       // Degrees per frame
#define UI_REFRESH_MS        16      // ~60fps LVGL tick
#define INPUT_POLL_MS        5       // Button/encoder poll interval
#define DEBOUNCE_MS          50      // Button debounce
