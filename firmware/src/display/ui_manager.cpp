#include "ui_manager.h"
#include "../config.h"
#include "../audio/audio_manager.h"
#include "../storage/sd_manager.h"
#include "../bluetooth/bt_manager.h"
#include <lvgl.h>
#include <Arduino.h>

// ── Lazy screen lifecycle ───────────────────────────────────
//
// RAM is the scarcest resource on the WROOM-32, so only the ACTIVE screen's
// widget tree exists. Switching screens rebuilds the target from small
// persistent state (song library pointer, BT device list, status string)
// and deletes the outgoing tree — slower screen changes, but the idle cost
// of a screen is zero. Album art (up to ART_MAX_BYTES) is freed whenever
// Now Playing is left and re-read from SD on return.
//
// Switches requested from LVGL event callbacks or the input task are
// deferred via pendingScreen and applied at the top of UI::update() on the
// UI task — deleting a screen from inside one of its own event handlers
// (or from another task) would be use-after-free territory.

static Screen active_screen = Screen::SONG_LIST;
static volatile int pendingScreen = -1;
static lv_group_t *cur_grp = nullptr;       // focus group of the active screen

// ── Persistent UI state (survives screen rebuilds) ──────────
static const std::vector<SongInfo> *songLib = nullptr;  // owned by main.cpp
static std::vector<BtDevice> btDevices;                 // capped at MAX_BT_DEVICES
static String btStatus = "Off";

// ── Input (virtual keypad fed by the input task) ────────────
static lv_indev_t *keypad_indev = nullptr;

#define KEYQ_SIZE 8
static volatile uint32_t keyQueue[KEYQ_SIZE];
static volatile uint8_t keyHead = 0, keyTail = 0;
static uint32_t curKey = 0;
static bool keyDown = false;

// ── Now Playing widgets (null unless that screen is active) ─
static lv_obj_t *np_title_label = nullptr;
static lv_obj_t *np_art_holder = nullptr;   // rotating circle (clips art)
static lv_obj_t *np_art_img = nullptr;      // album art image
static lv_obj_t *np_progress_arc = nullptr;
static lv_obj_t *np_time_label = nullptr;
static lv_obj_t *np_mode_label = nullptr;
static lv_obj_t *np_vol_label = nullptr;
static lv_obj_t *np_bt_label = nullptr;

// Change caches — lv_label_set_text reallocates the label buffer on every
// call, so the per-frame setters must early-return when nothing changed or
// they churn the heap at 60Hz.
static const SongInfo *np_last_song = nullptr;
static bool np_song_valid = false;
static uint32_t np_last_cur = UINT32_MAX, np_last_total = UINT32_MAX;
static int np_last_vol = -1;
static uint8_t np_last_mode = 0xFF;

// Album art image descriptor (loaded from .art file)
static lv_img_dsc_t art_dsc;
static uint8_t *art_data = nullptr;
static String art_path;                     // path art_data was loaded for
static int16_t vinyl_angle = 0;

// ── Song list widgets ───────────────────────────────────────
static lv_obj_t *sl_list = nullptr;

// ── Bluetooth widgets ───────────────────────────────────────
static lv_obj_t *bt_status_label = nullptr;
static lv_obj_t *bt_list = nullptr;

// ── Forward declarations ────────────────────────────────────
static lv_obj_t *createNowPlayingScreen();
static lv_obj_t *createSongListScreen();
static lv_obj_t *createSettingsScreen();
static lv_obj_t *createBluetoothScreen();
static void songListClickCb(lv_event_t *e);

// ── Colors (warm vinyl palette) ─────────────────────────────
#define COL_BG          lv_color_hex(0x1a0f08)
#define COL_SURFACE     lv_color_hex(0x2c1a0e)
#define COL_ACCENT      lv_color_hex(0xd4a647)
#define COL_TEXT        lv_color_hex(0xf0e6d0)
#define COL_TEXT_DIM    lv_color_hex(0x8b6340)
#define COL_VINYL       lv_color_hex(0x1a1a1a)

// ── Styles (initialized once; shared across screen rebuilds) ─
static lv_style_t style_bg;
static lv_style_t style_title;
static lv_style_t style_subtitle;
static lv_style_t style_list_btn;
static lv_style_t style_list_btn_focus;

static void initStyles() {
    // Background
    lv_style_init(&style_bg);
    lv_style_set_bg_color(&style_bg, COL_BG);
    lv_style_set_bg_opa(&style_bg, LV_OPA_COVER);
    lv_style_set_text_color(&style_bg, COL_TEXT);

    // Title text
    lv_style_init(&style_title);
    lv_style_set_text_color(&style_title, COL_TEXT);
    lv_style_set_text_font(&style_title, &lv_font_montserrat_16);

    // Subtitle text
    lv_style_init(&style_subtitle);
    lv_style_set_text_color(&style_subtitle, COL_TEXT_DIM);
    lv_style_set_text_font(&style_subtitle, &lv_font_montserrat_12);

    // List button
    lv_style_init(&style_list_btn);
    lv_style_set_bg_color(&style_list_btn, COL_SURFACE);
    lv_style_set_bg_opa(&style_list_btn, LV_OPA_COVER);
    lv_style_set_text_color(&style_list_btn, COL_TEXT);
    lv_style_set_border_width(&style_list_btn, 0);
    lv_style_set_pad_ver(&style_list_btn, 8);

    // Focused list button (encoder highlight)
    lv_style_init(&style_list_btn_focus);
    lv_style_set_bg_color(&style_list_btn_focus, COL_ACCENT);
    lv_style_set_text_color(&style_list_btn_focus, COL_BG);
}

// Style a list button and register it with a focus group
static void setupListBtn(lv_obj_t *btn, lv_group_t *grp) {
    lv_obj_add_style(btn, &style_list_btn, 0);
    lv_obj_add_style(btn, &style_list_btn_focus, LV_STATE_FOCUS_KEY);
    lv_obj_add_style(btn, &style_list_btn_focus, LV_STATE_FOCUSED);
    if (grp) lv_group_add_obj(grp, btn);
}

// ── Virtual keypad indev ────────────────────────────────────

static void keypadReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    if (keyDown) {
        // Release the previously reported key
        data->key = curKey;
        data->state = LV_INDEV_STATE_RELEASED;
        keyDown = false;
    } else if (keyTail != keyHead) {
        curKey = keyQueue[keyTail];
        keyTail = (keyTail + 1) % KEYQ_SIZE;
        data->key = curKey;
        data->state = LV_INDEV_STATE_PRESSED;
        keyDown = true;
    } else {
        data->key = curKey;
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->continue_reading = keyDown || (keyTail != keyHead);
}

void UI::sendKey(uint32_t lvKey) {
    uint8_t next = (keyHead + 1) % KEYQ_SIZE;
    if (next != keyTail) {
        keyQueue[keyHead] = lvKey;
        keyHead = next;
    }
}

// ═══════════════════════════════════════════════════════════
// ALBUM ART
// ═══════════════════════════════════════════════════════════

static void showDefaultArt() {
    if (np_art_img) lv_obj_add_flag(np_art_img, LV_OBJ_FLAG_HIDDEN);
}

static void freeArt() {
    if (art_data) {
        lv_img_cache_invalidate_src(&art_dsc);
        free(art_data);
        art_data = nullptr;
    }
    art_path = "";
}

static void loadArtFor(const SongInfo *song) {
    if (art_path == song->filepath) return;   // already loaded
    art_path = song->filepath;

    if (!song->hasArt) {
        showDefaultArt();
        return;
    }

    size_t artSize = 0;
    uint8_t *newArt = Storage::loadArtFile(song->filepath, artSize);
    if (!newArt) {
        showDefaultArt();
        return;
    }

    // Art files are square RGB565 — side length from byte count
    uint32_t side = (uint32_t)sqrtf(artSize / 2.0f);
    if (side * side * 2 != artSize || side > ART_MAX_SIDE) {
        Serial.printf("[UI] Bad art file size %u for %s\n", (unsigned)artSize, song->filepath.c_str());
        free(newArt);
        showDefaultArt();
        return;
    }

    art_dsc.header.always_zero = 0;
    art_dsc.header.w = side;
    art_dsc.header.h = side;
    art_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    art_dsc.data_size = artSize;
    art_dsc.data = newArt;

    lv_img_cache_invalidate_src(&art_dsc);
    lv_img_set_src(np_art_img, &art_dsc);
    // Scale to fill the 90px holder
    lv_img_set_zoom(np_art_img, (256 * 90) / side);
    lv_obj_clear_flag(np_art_img, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(np_art_img);

    // Free the previous buffer only after the new src is applied
    if (art_data) free(art_data);
    art_data = newArt;
}

// ═══════════════════════════════════════════════════════════
// NOW PLAYING SCREEN
// ═══════════════════════════════════════════════════════════

static lv_obj_t *createNowPlayingScreen() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &style_bg, 0);

    // ── Vinyl record (circular background) ──────────────────
    // Outer vinyl circle
    lv_obj_t *vinyl_ring = lv_obj_create(scr);
    lv_obj_set_size(vinyl_ring, 180, 180);
    lv_obj_align(vinyl_ring, LV_ALIGN_CENTER, 0, -15);
    lv_obj_set_style_radius(vinyl_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(vinyl_ring, COL_VINYL, 0);
    lv_obj_set_style_bg_opa(vinyl_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(vinyl_ring, COL_TEXT_DIM, 0);
    lv_obj_set_style_border_width(vinyl_ring, 2, 0);
    lv_obj_clear_flag(vinyl_ring, LV_OBJ_FLAG_SCROLLABLE);

    // Album art holder (center circle, rotates; clips the art square)
    np_art_holder = lv_obj_create(vinyl_ring);
    lv_obj_set_size(np_art_holder, 90, 90);
    lv_obj_align(np_art_holder, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(np_art_holder, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(np_art_holder, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(np_art_holder, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(np_art_holder, 0, 0);
    lv_obj_set_style_pad_all(np_art_holder, 0, 0);
    lv_obj_set_style_clip_corner(np_art_holder, true, 0);
    lv_obj_clear_flag(np_art_holder, LV_OBJ_FLAG_SCROLLABLE);

    // Album art image (hidden until a song with art plays)
    np_art_img = lv_img_create(np_art_holder);
    lv_obj_center(np_art_img);
    lv_obj_add_flag(np_art_img, LV_OBJ_FLAG_HIDDEN);

    // Spindle hole
    lv_obj_t *spindle = lv_obj_create(np_art_holder);
    lv_obj_set_size(spindle, 8, 8);
    lv_obj_align(spindle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(spindle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(spindle, COL_VINYL, 0);
    lv_obj_set_style_bg_opa(spindle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(spindle, 0, 0);

    // ── Progress arc (around the vinyl) ─────────────────────
    np_progress_arc = lv_arc_create(scr);
    lv_obj_set_size(np_progress_arc, 200, 200);
    lv_obj_align(np_progress_arc, LV_ALIGN_CENTER, 0, -15);
    lv_arc_set_rotation(np_progress_arc, 270);
    lv_arc_set_range(np_progress_arc, 0, 100);
    lv_arc_set_value(np_progress_arc, 0);
    lv_arc_set_bg_angles(np_progress_arc, 0, 360);
    lv_obj_set_style_arc_color(np_progress_arc, COL_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_arc_color(np_progress_arc, COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(np_progress_arc, 4, LV_PART_MAIN);
    lv_obj_set_style_arc_width(np_progress_arc, 4, LV_PART_INDICATOR);
    lv_obj_remove_style(np_progress_arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(np_progress_arc, LV_OBJ_FLAG_CLICKABLE);

    // ── Text labels (below vinyl) ───────────────────────────
    np_title_label = lv_label_create(scr);
    lv_obj_add_style(np_title_label, &style_title, 0);
    lv_label_set_text(np_title_label, "No Song");
    lv_label_set_long_mode(np_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(np_title_label, 200);
    lv_obj_align(np_title_label, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_set_style_text_align(np_title_label, LV_TEXT_ALIGN_CENTER, 0);

    // Time label
    np_time_label = lv_label_create(scr);
    lv_obj_add_style(np_time_label, &style_subtitle, 0);
    lv_label_set_text(np_time_label, "0:00 / 0:00");
    lv_obj_align(np_time_label, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_style_text_align(np_time_label, LV_TEXT_ALIGN_CENTER, 0);

    // Play mode indicator (top-left)
    np_mode_label = lv_label_create(scr);
    lv_obj_add_style(np_mode_label, &style_subtitle, 0);
    lv_label_set_text(np_mode_label, LV_SYMBOL_LOOP);
    lv_obj_align(np_mode_label, LV_ALIGN_TOP_LEFT, 8, 8);

    // Volume indicator (top-right)
    np_vol_label = lv_label_create(scr);
    lv_obj_add_style(np_vol_label, &style_subtitle, 0);
    lv_label_set_text(np_vol_label, LV_SYMBOL_VOLUME_MAX);
    lv_obj_align(np_vol_label, LV_ALIGN_TOP_RIGHT, -8, 8);

    // Bluetooth status (top-center)
    np_bt_label = lv_label_create(scr);
    lv_obj_add_style(np_bt_label, &style_subtitle, 0);
    lv_label_set_text(np_bt_label, BtMgr::isConnected() ? LV_SYMBOL_BLUETOOTH : "");
    lv_obj_align(np_bt_label, LV_ALIGN_TOP_MID, 0, 20);

    // Invalidate the change caches so the per-frame setters repopulate
    // the fresh widgets on the next UI tick
    np_song_valid = false;
    np_last_cur = np_last_total = UINT32_MAX;
    np_last_vol = -1;
    np_last_mode = 0xFF;

    return scr;
}

// ═══════════════════════════════════════════════════════════
// SONG LIST SCREEN
// ═══════════════════════════════════════════════════════════

static void songListClickCb(lv_event_t *e) {
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    AudioMgr::play(idx);
    UI::showScreen(Screen::NOW_PLAYING);   // deferred — safe from this callback
}

static void populateSongList() {
    if (!sl_list) return;
    lv_obj_clean(sl_list);
    if (!songLib) return;

    // Guard against heap exhaustion: LVGL uses system malloc, and on the
    // no-PSRAM WROOM-32 the Classic-BT/A2DP stack leaves little headroom.
    // Each list button is ~700 bytes; stop before we run the heap dry so we
    // degrade gracefully instead of dereferencing a NULL LVGL allocation.
    const uint32_t HEAP_FLOOR = 20000;
    size_t built = 0;
    for (size_t i = 0; i < songLib->size(); i++) {
        if (ESP.getMaxAllocHeap() < HEAP_FLOOR) {
            Serial.printf("[UI] Song list truncated at %u/%u (low heap=%u)\n",
                          (unsigned)i, (unsigned)songLib->size(), ESP.getFreeHeap());
            break;
        }
        const SongInfo &song = (*songLib)[i];
        const char *icon = song.hasArt ? LV_SYMBOL_IMAGE : LV_SYMBOL_AUDIO;
        lv_obj_t *btn = lv_list_add_btn(sl_list, icon, Storage::songTitle(song).c_str());
        setupListBtn(btn, cur_grp);
        lv_obj_add_event_cb(btn, songListClickCb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
        built++;
    }
    Serial.printf("[UI] Built %u list buttons, free heap=%u largest=%u\n",
                  (unsigned)built, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

static lv_obj_t *createSongListScreen() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &style_bg, 0);
    cur_grp = lv_group_create();

    // Header
    lv_obj_t *header = lv_label_create(scr);
    lv_obj_add_style(header, &style_title, 0);
    lv_label_set_text(header, LV_SYMBOL_AUDIO " Library");
    lv_obj_set_style_text_color(header, COL_ACCENT, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);

    // Scrollable list
    sl_list = lv_list_create(scr);
    lv_obj_set_size(sl_list, 230, 200);
    lv_obj_align(sl_list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(sl_list, COL_BG, 0);
    lv_obj_set_style_bg_opa(sl_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sl_list, 0, 0);
    lv_obj_set_style_pad_row(sl_list, 2, 0);

    populateSongList();
    return scr;
}

// ═══════════════════════════════════════════════════════════
// SETTINGS SCREEN
// ═══════════════════════════════════════════════════════════

static void btMenuCb(lv_event_t *e) {
    UI::showScreen(Screen::BLUETOOTH);
}

static lv_obj_t *createSettingsScreen() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &style_bg, 0);
    cur_grp = lv_group_create();

    // Header
    lv_obj_t *header = lv_label_create(scr);
    lv_obj_add_style(header, &style_title, 0);
    lv_label_set_text(header, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(header, COL_ACCENT, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);

    // Settings list
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 230, 200);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);

    // Audio output — Bluetooth only (wired output removed)
    lv_obj_t *out_btn = lv_list_add_btn(list, LV_SYMBOL_VOLUME_MAX, "Output: Bluetooth");
    setupListBtn(out_btn, cur_grp);
    lv_obj_clear_flag(out_btn, LV_OBJ_FLAG_CLICKABLE);

    // Bluetooth device menu
    lv_obj_t *bt_btn = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, "Bluetooth Devices");
    setupListBtn(bt_btn, cur_grp);
    lv_obj_add_event_cb(bt_btn, btMenuCb, LV_EVENT_CLICKED, nullptr);

    // About
    lv_obj_t *about_btn = lv_list_add_btn(list, LV_SYMBOL_HOME, "Pseudo Vinyl v2.0");
    setupListBtn(about_btn, cur_grp);

    return scr;
}

// ═══════════════════════════════════════════════════════════
// BLUETOOTH SCREEN
// ═══════════════════════════════════════════════════════════

static void btDeviceClickCb(lv_event_t *e) {
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (idx < btDevices.size()) {
        BtMgr::setTarget(btDevices[idx].name);
        UI::setBtStatus("Connecting: " + btDevices[idx].name);
    }
}

static void populateBtList() {
    if (!bt_list) return;
    lv_obj_clean(bt_list);

    for (size_t i = 0; i < btDevices.size(); i++) {
        lv_obj_t *btn = lv_list_add_btn(bt_list, LV_SYMBOL_BLUETOOTH, btDevices[i].name.c_str());
        setupListBtn(btn, cur_grp);
        lv_obj_add_event_cb(btn, btDeviceClickCb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
    if (btDevices.empty()) {
        lv_obj_t *btn = lv_list_add_btn(bt_list, LV_SYMBOL_REFRESH, "Searching...");
        lv_obj_add_style(btn, &style_list_btn, 0);
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    }
}

static lv_obj_t *createBluetoothScreen() {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &style_bg, 0);
    cur_grp = lv_group_create();

    // Header
    lv_obj_t *header = lv_label_create(scr);
    lv_obj_add_style(header, &style_title, 0);
    lv_label_set_text(header, LV_SYMBOL_BLUETOOTH " Bluetooth");
    lv_obj_set_style_text_color(header, COL_ACCENT, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);

    // Status line
    bt_status_label = lv_label_create(scr);
    lv_obj_add_style(bt_status_label, &style_subtitle, 0);
    lv_label_set_text(bt_status_label, btStatus.c_str());
    lv_label_set_long_mode(bt_status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(bt_status_label, 200);
    lv_obj_set_style_text_align(bt_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(bt_status_label, LV_ALIGN_TOP_MID, 0, 32);

    // Discovered device list
    bt_list = lv_list_create(scr);
    lv_obj_set_size(bt_list, 230, 180);
    lv_obj_align(bt_list, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(bt_list, COL_BG, 0);
    lv_obj_set_style_bg_opa(bt_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bt_list, 0, 0);
    lv_obj_set_style_pad_row(bt_list, 2, 0);

    populateBtList();
    return scr;
}

// ═══════════════════════════════════════════════════════════
// SCREEN LIFECYCLE
// ═══════════════════════════════════════════════════════════

// Null out all widget references and free per-screen data. The objects
// themselves die with the outgoing screen's lv_obj_del.
static void clearWidgetRefs() {
    np_title_label = np_art_holder = np_art_img = np_progress_arc = nullptr;
    np_time_label = np_mode_label = np_vol_label = np_bt_label = nullptr;
    sl_list = nullptr;
    bt_status_label = nullptr;
    bt_list = nullptr;
    freeArt();   // reclaim up to ART_MAX_BYTES while off the Now Playing screen
}

// Delete the old tree + focus group, then build and load the target screen.
// Must run on the UI task, outside any LVGL event callback.
//
// Delete-BEFORE-build matters: peak heap is max(old, new) instead of their
// sum. Building first once crashed the device — the 15-button song list plus
// a fresh Now Playing tree plus the just-started MP3 decoder ran the heap to
// zero, and LVGL's unchecked lv_mem_realloc in lv_obj_class_create_obj turned
// that into a NULL write. LVGL can't have its active screen deleted under it,
// so a bare placeholder screen (~150B) bridges the gap (one blank frame).
static void doShowScreen(Screen screen) {
    lv_obj_t *old = lv_scr_act();
    lv_group_t *oldGrp = cur_grp;

    clearWidgetRefs();
    cur_grp = nullptr;

    lv_obj_t *placeholder = lv_obj_create(NULL);
    lv_obj_add_style(placeholder, &style_bg, 0);
    if (keypad_indev) {
        lv_indev_reset(keypad_indev, nullptr);   // drop refs into the old tree
        lv_indev_set_group(keypad_indev, nullptr);
    }
    lv_scr_load(placeholder);
    if (old && old != placeholder) lv_obj_del(old);
    if (oldGrp) lv_group_del(oldGrp);

    lv_obj_t *scr = nullptr;
    switch (screen) {
        case Screen::NOW_PLAYING: scr = createNowPlayingScreen(); break;
        case Screen::SONG_LIST:   scr = createSongListScreen();   break;
        case Screen::SETTINGS:    scr = createSettingsScreen();   break;
        case Screen::BLUETOOTH:   scr = createBluetoothScreen();  break;
    }

    active_screen = screen;
    if (keypad_indev) lv_indev_set_group(keypad_indev, cur_grp);
    lv_scr_load(scr);   // no fade — both trees would have to coexist longer
    lv_obj_del(placeholder);

    Serial.printf("[UI] Screen %d shown (free=%u largest=%u)\n",
                  (int)screen, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
}

// ═══════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════

void UI::init() {
    initStyles();

    // Virtual keypad driven by the encoder/buttons
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_KEYPAD;
    indev_drv.read_cb = keypadReadCb;
    keypad_indev = lv_indev_drv_register(&indev_drv);

    // Start on song list (init runs before the tasks — direct build is safe)
    doShowScreen(Screen::SONG_LIST);
    Serial.println("[UI] Ready (lazy screens — only the active one is built)");
}

void UI::showScreen(Screen screen) {
    // Route input against the new screen immediately; the rebuild happens
    // on the UI task at the next UI::update() (see pendingScreen note above)
    active_screen = screen;
    pendingScreen = (int)screen;
}

Screen UI::activeScreen() {
    return active_screen;
}

void UI::update() {
    // Apply a deferred screen switch (requested from input task or an LVGL
    // event callback) now that we're safely outside lv_timer_handler
    if (pendingScreen >= 0) {
        Screen s = (Screen)pendingScreen;
        pendingScreen = -1;
        doShowScreen(s);
    }

    // Spin the vinyl if playing
    if (AudioMgr::isPlaying() && np_art_holder) {
        vinyl_angle = (vinyl_angle + VINYL_SPIN_SPEED_DEG) % 360;
        lv_obj_set_style_transform_angle(np_art_holder, vinyl_angle * 10, 0);
        lv_obj_set_style_transform_pivot_x(np_art_holder, 45, 0);
        lv_obj_set_style_transform_pivot_y(np_art_holder, 45, 0);
    }
}

void UI::setSongList(const std::vector<SongInfo> *songs) {
    songLib = songs;
    if (sl_list) populateSongList();   // song-list screen currently shown
}

void UI::setNowPlaying(const SongInfo *song, bool playing) {
    if (!np_title_label) return;
    if (np_song_valid && song == np_last_song) return;   // no 60Hz label churn
    np_song_valid = true;
    np_last_song = song;

    if (song) {
        lv_label_set_text(np_title_label, Storage::songTitle(*song).c_str());
        loadArtFor(song);
    } else {
        lv_label_set_text(np_title_label, "No Song");
        art_path = "";
        showDefaultArt();
    }
}

void UI::setProgress(uint32_t currentSec, uint32_t totalSec) {
    if (!np_progress_arc || !np_time_label) return;
    if (currentSec == np_last_cur && totalSec == np_last_total) return;
    np_last_cur = currentSec;
    np_last_total = totalSec;

    int pct = (totalSec > 0) ? (currentSec * 100 / totalSec) : 0;
    lv_arc_set_value(np_progress_arc, pct);

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu:%02lu / %lu:%02lu",
        currentSec / 60, currentSec % 60,
        totalSec / 60, totalSec % 60);
    lv_label_set_text(np_time_label, buf);
}

void UI::setVolume(int vol, int maxVol) {
    if (!np_vol_label) return;
    if (vol == np_last_vol) return;
    np_last_vol = vol;
    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d", LV_SYMBOL_VOLUME_MAX, vol);
    lv_label_set_text(np_vol_label, buf);
}

void UI::setPlayModeIndicator(PlayMode mode) {
    if (!np_mode_label) return;
    if ((uint8_t)mode == np_last_mode) return;
    np_last_mode = (uint8_t)mode;
    switch (mode) {
        case PlayMode::NORMAL:     lv_label_set_text(np_mode_label, LV_SYMBOL_LOOP);       break;
        case PlayMode::SHUFFLE:    lv_label_set_text(np_mode_label, LV_SYMBOL_SHUFFLE);     break;
        case PlayMode::REPEAT_ALL: lv_label_set_text(np_mode_label, LV_SYMBOL_LOOP " All"); break;
        case PlayMode::REPEAT_ONE: lv_label_set_text(np_mode_label, LV_SYMBOL_LOOP " 1");   break;
    }
}

void UI::setBtStatus(const String &status) {
    btStatus = status;
    if (bt_status_label) {
        lv_label_set_text(bt_status_label, btStatus.c_str());
    }
    // Mirror connection state on the Now Playing icon
    if (np_bt_label) {
        lv_label_set_text(np_bt_label, BtMgr::isConnected() ? LV_SYMBOL_BLUETOOTH : "");
    }
}

void UI::setBtDevices(const std::vector<BtDevice> &devices) {
    btDevices = devices;   // small — capped at MAX_BT_DEVICES by BtMgr
    if (bt_list) populateBtList();
}
