#include "ui_manager.h"
#include "../config.h"
#include "../audio/audio_manager.h"
#include "../storage/sd_manager.h"
#include <lvgl.h>

// ── Screen objects ──────────────────────────────────────────
static lv_obj_t *scr_now_playing = nullptr;
static lv_obj_t *scr_song_list = nullptr;
static lv_obj_t *scr_settings = nullptr;

// ── Now Playing widgets ─────────────────────────────────────
static lv_obj_t *np_title_label = nullptr;
static lv_obj_t *np_artist_label = nullptr;
static lv_obj_t *np_vinyl_img = nullptr;
static lv_obj_t *np_progress_arc = nullptr;
static lv_obj_t *np_time_label = nullptr;
static lv_obj_t *np_mode_label = nullptr;
static lv_obj_t *np_vol_label = nullptr;
static lv_obj_t *np_play_icon = nullptr;

// Album art image descriptor (loaded from .art file)
static lv_img_dsc_t art_dsc;
static uint8_t *art_data = nullptr;
static int16_t vinyl_angle = 0;

// ── Song list widgets ───────────────────────────────────────
static lv_obj_t *sl_list = nullptr;
static std::vector<SongInfo> songList;

// ── Forward declarations ────────────────────────────────────
static void createNowPlayingScreen();
static void createSongListScreen();
static void createSettingsScreen();
static void songListClickCb(lv_event_t *e);

// ── Colors (warm vinyl palette) ─────────────────────────────
#define COL_BG          lv_color_hex(0x1a0f08)
#define COL_SURFACE     lv_color_hex(0x2c1a0e)
#define COL_ACCENT      lv_color_hex(0xd4a647)
#define COL_TEXT        lv_color_hex(0xf0e6d0)
#define COL_TEXT_DIM    lv_color_hex(0x8b6340)
#define COL_VINYL       lv_color_hex(0x1a1a1a)

// ── Styles ──────────────────────────────────────────────────
static lv_style_t style_bg;
static lv_style_t style_title;
static lv_style_t style_subtitle;
static lv_style_t style_list_btn;

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
}

// ═══════════════════════════════════════════════════════════
// NOW PLAYING SCREEN
// ═══════════════════════════════════════════════════════════

static void createNowPlayingScreen() {
    scr_now_playing = lv_obj_create(NULL);
    lv_obj_add_style(scr_now_playing, &style_bg, 0);

    // ── Vinyl record (circular background) ──────────────────
    // Outer vinyl circle
    lv_obj_t *vinyl_ring = lv_obj_create(scr_now_playing);
    lv_obj_set_size(vinyl_ring, 180, 180);
    lv_obj_align(vinyl_ring, LV_ALIGN_CENTER, 0, -15);
    lv_obj_set_style_radius(vinyl_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(vinyl_ring, COL_VINYL, 0);
    lv_obj_set_style_bg_opa(vinyl_ring, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(vinyl_ring, COL_TEXT_DIM, 0);
    lv_obj_set_style_border_width(vinyl_ring, 2, 0);
    lv_obj_clear_flag(vinyl_ring, LV_OBJ_FLAG_SCROLLABLE);

    // Album art placeholder (center circle, acts as "label")
    np_vinyl_img = lv_obj_create(vinyl_ring);
    lv_obj_set_size(np_vinyl_img, 90, 90);
    lv_obj_align(np_vinyl_img, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(np_vinyl_img, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(np_vinyl_img, COL_ACCENT, 0);
    lv_obj_set_style_bg_opa(np_vinyl_img, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(np_vinyl_img, 0, 0);
    lv_obj_clear_flag(np_vinyl_img, LV_OBJ_FLAG_SCROLLABLE);

    // Spindle hole
    lv_obj_t *spindle = lv_obj_create(np_vinyl_img);
    lv_obj_set_size(spindle, 8, 8);
    lv_obj_align(spindle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(spindle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(spindle, COL_VINYL, 0);
    lv_obj_set_style_bg_opa(spindle, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(spindle, 0, 0);

    // ── Progress arc (around the vinyl) ─────────────────────
    np_progress_arc = lv_arc_create(scr_now_playing);
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
    np_title_label = lv_label_create(scr_now_playing);
    lv_obj_add_style(np_title_label, &style_title, 0);
    lv_label_set_text(np_title_label, "No Song");
    lv_label_set_long_mode(np_title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(np_title_label, 200);
    lv_obj_align(np_title_label, LV_ALIGN_BOTTOM_MID, 0, -48);
    lv_obj_set_style_text_align(np_title_label, LV_TEXT_ALIGN_CENTER, 0);

    np_artist_label = lv_label_create(scr_now_playing);
    lv_obj_add_style(np_artist_label, &style_subtitle, 0);
    lv_label_set_text(np_artist_label, "");
    lv_label_set_long_mode(np_artist_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(np_artist_label, 180);
    lv_obj_align(np_artist_label, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_text_align(np_artist_label, LV_TEXT_ALIGN_CENTER, 0);

    // Time label
    np_time_label = lv_label_create(scr_now_playing);
    lv_obj_add_style(np_time_label, &style_subtitle, 0);
    lv_label_set_text(np_time_label, "0:00 / 0:00");
    lv_obj_align(np_time_label, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_text_align(np_time_label, LV_TEXT_ALIGN_CENTER, 0);

    // Play mode indicator (top-left)
    np_mode_label = lv_label_create(scr_now_playing);
    lv_obj_add_style(np_mode_label, &style_subtitle, 0);
    lv_label_set_text(np_mode_label, LV_SYMBOL_LOOP);
    lv_obj_align(np_mode_label, LV_ALIGN_TOP_LEFT, 8, 8);

    // Volume indicator (top-right)
    np_vol_label = lv_label_create(scr_now_playing);
    lv_obj_add_style(np_vol_label, &style_subtitle, 0);
    lv_label_set_text(np_vol_label, LV_SYMBOL_VOLUME_MAX);
    lv_obj_align(np_vol_label, LV_ALIGN_TOP_RIGHT, -8, 8);
}

// ═══════════════════════════════════════════════════════════
// SONG LIST SCREEN
// ═══════════════════════════════════════════════════════════

static void createSongListScreen() {
    scr_song_list = lv_obj_create(NULL);
    lv_obj_add_style(scr_song_list, &style_bg, 0);

    // Header
    lv_obj_t *header = lv_label_create(scr_song_list);
    lv_obj_add_style(header, &style_title, 0);
    lv_label_set_text(header, LV_SYMBOL_AUDIO " Library");
    lv_obj_set_style_text_color(header, COL_ACCENT, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);

    // Scrollable list
    sl_list = lv_list_create(scr_song_list);
    lv_obj_set_size(sl_list, 230, 200);
    lv_obj_align(sl_list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(sl_list, COL_BG, 0);
    lv_obj_set_style_bg_opa(sl_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sl_list, 0, 0);
    lv_obj_set_style_pad_row(sl_list, 2, 0);
}

static void songListClickCb(lv_event_t *e) {
    lv_obj_t *btn = lv_event_get_target(e);
    uint32_t idx = (uint32_t)(uintptr_t)lv_event_get_user_data(e);

    AudioMgr::play(idx);
    UI::showScreen(Screen::NOW_PLAYING);
}

// ═══════════════════════════════════════════════════════════
// SETTINGS SCREEN
// ═══════════════════════════════════════════════════════════

static void createSettingsScreen() {
    scr_settings = lv_obj_create(NULL);
    lv_obj_add_style(scr_settings, &style_bg, 0);

    // Header
    lv_obj_t *header = lv_label_create(scr_settings);
    lv_obj_add_style(header, &style_title, 0);
    lv_label_set_text(header, LV_SYMBOL_SETTINGS " Settings");
    lv_obj_set_style_text_color(header, COL_ACCENT, 0);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 8);

    // Settings list
    lv_obj_t *list = lv_list_create(scr_settings);
    lv_obj_set_size(list, 230, 200);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 10);
    lv_obj_set_style_bg_color(list, COL_BG, 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(list, 0, 0);

    // Audio output
    lv_obj_t *audio_btn = lv_list_add_btn(list, LV_SYMBOL_VOLUME_MAX, "Audio: Wired (3.5mm)");
    lv_obj_add_style(audio_btn, &style_list_btn, 0);

    // Bluetooth (placeholder - grayed out)
    lv_obj_t *bt_btn = lv_list_add_btn(list, LV_SYMBOL_BLUETOOTH, "Bluetooth: Coming Soon");
    lv_obj_add_style(bt_btn, &style_list_btn, 0);
    lv_obj_set_style_text_color(bt_btn, COL_TEXT_DIM, 0);
    lv_obj_set_style_bg_color(bt_btn, lv_color_hex(0x1a0f08), 0);
    lv_obj_clear_flag(bt_btn, LV_OBJ_FLAG_CLICKABLE);

    // About
    lv_obj_t *about_btn = lv_list_add_btn(list, LV_SYMBOL_HOME, "Pseudo Vinyl v1.0");
    lv_obj_add_style(about_btn, &style_list_btn, 0);
}

// ═══════════════════════════════════════════════════════════
// PUBLIC API
// ═══════════════════════════════════════════════════════════

void UI::init() {
    initStyles();
    createNowPlayingScreen();
    createSongListScreen();
    createSettingsScreen();

    // Start on song list
    lv_scr_load(scr_song_list);
    Serial.println("[UI] Screens created");
}

void UI::showScreen(Screen screen) {
    lv_obj_t *target = nullptr;
    switch (screen) {
        case Screen::NOW_PLAYING: target = scr_now_playing; break;
        case Screen::SONG_LIST:   target = scr_song_list;   break;
        case Screen::SETTINGS:    target = scr_settings;     break;
    }
    if (target) {
        lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
    }
}

void UI::update() {
    // Spin the vinyl if playing
    if (AudioMgr::isPlaying()) {
        vinyl_angle = (vinyl_angle + VINYL_SPIN_SPEED_DEG) % 360;
        if (np_vinyl_img) {
            lv_obj_set_style_transform_angle(np_vinyl_img, vinyl_angle * 10, 0);
            lv_obj_set_style_transform_pivot_x(np_vinyl_img, 45, 0);
            lv_obj_set_style_transform_pivot_y(np_vinyl_img, 45, 0);
        }
    }
}

void UI::setSongList(const std::vector<SongInfo> &songs) {
    songList = songs;
    if (!sl_list) return;

    // Clear existing items
    lv_obj_clean(sl_list);

    for (size_t i = 0; i < songs.size(); i++) {
        const char *icon = songs[i].hasArt ? LV_SYMBOL_IMAGE : LV_SYMBOL_AUDIO;
        lv_obj_t *btn = lv_list_add_btn(sl_list, icon, songs[i].title.c_str());
        lv_obj_add_style(btn, &style_list_btn, 0);
        lv_obj_add_event_cb(btn, songListClickCb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    }
}

void UI::setNowPlaying(const SongInfo *song, bool playing) {
    if (!np_title_label) return;

    if (song) {
        lv_label_set_text(np_title_label, song->title.c_str());
        lv_label_set_text(np_artist_label, song->artist.c_str());

        // Load album art if available
        if (song->hasArt) {
            if (art_data) { free(art_data); art_data = nullptr; }
            size_t artSize = 0;
            art_data = Storage::loadArtFile(song->filepath, artSize);
            if (art_data && artSize == DISPLAY_WIDTH * DISPLAY_WIDTH * 2) {
                art_dsc.header.always_zero = 0;
                art_dsc.header.w = DISPLAY_WIDTH;
                art_dsc.header.h = DISPLAY_WIDTH;
                art_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
                art_dsc.data_size = artSize;
                art_dsc.data = art_data;
                // TODO: Apply as image source to vinyl center
            }
        }
    } else {
        lv_label_set_text(np_title_label, "No Song");
        lv_label_set_text(np_artist_label, "");
    }
}

void UI::setProgress(uint32_t currentSec, uint32_t totalSec) {
    if (!np_progress_arc || !np_time_label) return;

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
    char buf[16];
    snprintf(buf, sizeof(buf), "%s %d", LV_SYMBOL_VOLUME_MAX, vol);
    lv_label_set_text(np_vol_label, buf);
}

void UI::setPlayModeIndicator(PlayMode mode) {
    if (!np_mode_label) return;
    switch (mode) {
        case PlayMode::NORMAL:     lv_label_set_text(np_mode_label, LV_SYMBOL_LOOP);       break;
        case PlayMode::SHUFFLE:    lv_label_set_text(np_mode_label, LV_SYMBOL_SHUFFLE);     break;
        case PlayMode::REPEAT_ALL: lv_label_set_text(np_mode_label, LV_SYMBOL_LOOP " All"); break;
        case PlayMode::REPEAT_ONE: lv_label_set_text(np_mode_label, LV_SYMBOL_LOOP " 1");   break;
    }
}
