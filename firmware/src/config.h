#pragma once
/**
 * Pseudo Vinyl MP3 Player — Hardware Configuration
 * Board: ESP32-WROVER N4R8 (classic ESP32, 4MB flash, 8MB PSRAM)
 *
 * Pin rules for this module:
 *  - GPIO 6-11 are flash — never use
 *  - GPIO 16/17 are the PSRAM CS/CLK lines on WROVER — never use.
 *  - GPIO 12 (MTDI) strapping: a pull-up at boot bricks flash voltage —
 *    left unused. No longer at risk from the SD card either: SD moved to
 *    the SDMMC peripheral's 1-bit mode (below), which doesn't touch it.
 *  - GPIO 34/35/36/39 are INPUT-ONLY, no internal pull-ups
 *  - ADC2 pins can't do analogRead while Bluetooth is on → battery on ADC1
 *  - SD card uses the SDMMC peripheral, not SPI — on classic ESP32 its
 *    CLK/CMD/D0-D3 pins are fixed in the IOMUX (not GPIO-matrix routable,
 *    unlike the display's SPI pins below), so GPIO 14/15/2 aren't a free
 *    choice — see the SD Card block.
 *  - GPIO 0 is now unused by this design (it was the display's MOSI pin in
 *    an earlier revision — a boot-strapping pin, moved off deliberately).
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
// Routed through the GPIO matrix (none of these are VSPI/HSPI IOMUX-native
// pins), same as before — see platformio.ini's SPI_FREQUENCY note if the
// panel tears or shows garbage. No strapping-pin caution needed on this
// revision: none of MOSI/SCLK/CS/DC/RST below are boot-strap pins.
#define DISPLAY_WIDTH       240
#define DISPLAY_HEIGHT      240
#define PIN_TFT_MOSI        23      // SDA
#define PIN_TFT_SCLK        19      // SCL
#define PIN_TFT_CS          22
#define PIN_TFT_DC          21
#define PIN_TFT_RST         27
// Display module has no backlight pin (BL hardwired on-board); brightness
// control is unavailable.

// ── SD Card (SDMMC peripheral, 1-bit mode) ──────────────────
// Moved off SPI/HSPI to the ESP32's dedicated SDMMC peripheral (see
// sd_manager.cpp — SD_MMC library, not SD/SPI). On classic ESP32 the
// SDMMC pins are fixed in the IOMUX and NOT GPIO-matrix routable, so
// these three are the only usable values, not a preference:
#define PIN_SD_CLK          14
#define PIN_SD_CMD          15
#define PIN_SD_D0           2
// 1-bit mode only uses the three pins above — D1/D2/D3 (GPIO 4/12/13 on
// this SoC) stay fully free for other use. In particular this is why
// GPIO 12's old strapping caution no longer applies: nothing drives it.

// ── Audio: Bluetooth A2DP only ──────────────────────────────
// Wired I2S/PCM5102 output was removed back on the no-PSRAM WROOM-32: the
// Classic-BT + A2DP stack (~108KB) plus SD/FATFS + LVGL UI left no room for
// a second output path. Output is Bluetooth-only. The WROVER's PSRAM would
// now make a second path affordable, but re-adding it is out of scope here
// and the pins were reclaimed for the rewired display/SD.

// ── Rotary Encoder (KY-040) ─────────────────────────────────
#define PIN_ENC_A           32
#define PIN_ENC_B           33
#define PIN_ENC_SW          5       // moved off 27 — now TFT RST
                                    // (GPIO 5 freed when TFT SCLK moved to 19)

// ── Buttons ─────────────────────────────────────────────────
#define PIN_BTN_PLAY        13
#define PIN_BTN_NEXT        18      // moved off 14 — now SD CLK (SDMMC)
                                    // (GPIO 18 freed when SD moved off SPI)
#define PIN_BTN_PREV        4

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
#define VINYL_SPIN_SPEED_DEG 1       // Degrees per frame — was 2 (120°/s);
                                      // halved because the bigger per-frame
                                      // jump looked jittery even at a
                                      // confirmed 60Hz redraw (LV_DISP_DEF_REFR_PERIOD)
#define UI_REFRESH_MS        16      // ~60fps LVGL tick
#define INPUT_POLL_MS        5       // Button/encoder poll interval
#define DEBOUNCE_MS          50      // Button debounce
