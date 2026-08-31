# Spotify → YouTube → SD-Card Sync Pipeline

## Context

Today, getting music onto the Pseudo Vinyl player is fully manual: the user rips/sources MP3s themselves, runs `tools/prescale_art/prescale_art.py` (or its GUI) to generate `.art` album-art files, and drags folders onto the SD card by hand. The user wants to remove that manual sourcing step by linking their Spotify account, browsing their playlists, and having the tool assemble ready-to-copy playlist folders automatically.

Spotify's API cannot serve actual audio (DRM/ToS), so the pipeline sources audio via YouTube search + `yt-dlp` instead, using Spotify only for playlist/track metadata and album art. This was confirmed with the user, along with two other decisions baked into this plan:
- **Spotify auth**: one Spotify Developer app registered by the project owner, Client ID shipped baked-in, PKCE flow (no secret) — fine at personal/family scale.
- **ffmpeg**: documented as a prerequisite (like the existing Python check in `run.bat`), not bundled into the exe.
- **Library caps**: `MAX_SONGS = 15` per playlist folder and `MAX_PLAYLISTS = 8` (`firmware/src/config.h:96,102`) are hard ESP32 RAM constraints, not soft limits — the tool must warn explicitly when a selection exceeds them, never silently truncate without telling the user. **Note the off-by-one on playlists**: `MAX_PLAYLISTS = 8` counts the synthesized "All Songs" entry, so only **7 user folders** are ever shown on-device (see next section) — the tool's effective folder cap is 7, not 8.

This extends the existing desktop tool (`tools/prescale_art/`) rather than creating a separate app, reusing its threading/progress/logging patterns and its existing art-conversion pipeline as the final step.

## Existing architecture (verified)

- **Firmware has no manifest files** — everything is derived live from the SD filesystem. `Storage::scanMusic()` (`firmware/src/storage/sd_manager.cpp:66`) recursively scans a folder for `*.mp3`, capped at `MAX_SONGS=15`. `Storage::scanPlaylists()` (`sd_manager.cpp:100`) seeds the list with a synthesized "All Songs" entry for the SD root, then loops `while (lists.size() < MAX_PLAYLISTS)` adding top-level folders — so **only 7 folder-playlists are ever scanned** (the 8th slot is always "All Songs"), and the tool must never create a literal `All Songs/` folder. Two consequences the tool must respect: (a) the effective folder cap is **7**, not 8; (b) the cap is applied *during raw filesystem enumeration, before* the alphabetical sort at `sd_manager.cpp:132`, so *which* 7 folders survive on an over-capacity card depends on FAT directory order, not name — non-deterministic. The tool must therefore enforce the 7-folder limit upstream (warn + block) rather than trusting the firmware to drop the "right" ones.
- **Album art** = `<songname>.art` sibling file, raw RGB565, up to 240×240 (`ART_MAX_SIDE`, `config.h:114`).
- **`prescale_art.py`** exposes reusable pure functions: `extract_album_art()`, `resize_and_crop()`, `image_to_rgb565_bytes()`. No existing code reads/writes ID3 *text* tags (TIT2/TPE1/TALB) — only binary APIC art.
- **`prescale_art_gui.py`** is one 702-line `PseudoVinylConverter` tkinter class. Reusable patterns: background `threading.Thread(daemon=True)` worker, `root.after(0, ...)` UI marshaling, polled `cancel_requested` flag, color-tagged `tk.Text` activity log, canvas progress bar, hand-rolled vintage palette dict `C`.
- **Packaging**: `PseudoVinylConverter.spec` (PyInstaller, windowed exe) + `run.bat` (checks Python, `pip install -r requirements.txt`, launches GUI).

## Approach

### New subpackage: `tools/prescale_art/spotify_sync/`

- `spotify_config.py` — `SPOTIFY_CLIENT_ID` constant (public, PKCE, comment explaining a fork maintainer just swaps this line + registers a matching redirect URI), `REDIRECT_URI = "http://127.0.0.1:43813/callback"`, token-cache path helper (`%APPDATA%\PseudoVinylConverter\spotify_token_cache.json`, `Path.home()/.pseudovinyl` fallback).
- `spotify_auth.py` — PKCE login: starts a local loopback `HTTPServer` in a background thread, opens the system browser via `webbrowser.open()`, captures the redirect (`code`/`state`), exchanges via `spotipy.oauth2.SpotifyPKCE` (used purely as a token/cache manager — its own blocking `input()`-based flow is unusable from a GUI, so we drive the browser/redirect ourselves), persists tokens via `CacheFileHandler`. Exposes `login()`, `is_linked()`, `unlink()`. 120s timeout with a clear error if the browser flow is abandoned.
- `spotify_client.py` — thin wrappers over an authenticated `spotipy.Spotify`: `list_user_playlists()`, `get_playlist_tracks()` (paginates via `sp.next()`), normalized `TrackInfo`/`PlaylistSummary` dataclasses (title, artist, album, duration_ms, art_url, spotify_id). Skips locally-unavailable tracks with a log note rather than crashing the fetch.
- `youtube_search.py` — pure functions: `build_search_query()`, `search_candidates()` (yt-dlp `ytsearch5:`, `extract_flat='in_playlist'`, metadata-only, no download), `score_candidate()`, `pick_best_match()`. Matching strategy: hard-filter candidates whose duration differs from Spotify's by more than max(12s, 8%); rank survivors by duration closeness + `difflib.SequenceMatcher` title/uploader similarity, with a penalty (not an outright ban) for blacklist terms (`live`, `cover`, `remix`, `8d audio`, `sped up`, `nightcore`, `karaoke`) unless that term also appears in the actual Spotify track/album name. Below a score floor → "no match", logged and skipped, playlist continues.
- `youtube_download.py` — `download_audio()` wrapping `yt_dlp.YoutubeDL` with an mp3-extraction postprocessor and a `progress_hooks` callback; raises a clear typed error if ffmpeg is missing.
- `tagger.py` — `fetch_art_bytes()` (plain `requests.get` on the Spotify art URL), `write_tags()` using `mutagen.id3.ID3`/`TIT2`/`TPE1`/`TALB`/`APIC` (new code — first text-tag writing in the project).
- `naming.py` — pure filesystem-safe naming helpers (`sanitize_playlist_name`, `sanitize_song_title`, `track_filename`), split out from `sync_pipeline.py` during implementation so the sanitization edge cases (reserved names, leading dot, "All Songs" collision, trailing dots, empty-after-strip) are independently unit-testable.
- `limits.py` — `MAX_SONGS_PER_PLAYLIST = 15`, `MAX_FOLDER_PLAYLISTS = 7` (**not 8** — the firmware's `MAX_PLAYLISTS = 8` includes the synthesized "All Songs" slot, leaving room for only 7 real folders; see "Existing architecture"), commented as mirroring `firmware/src/config.h` — update together if the firmware RAM budget ever changes, and re-check the "All Songs" off-by-one if `scanPlaylists` is ever restructured.
- `sync_pipeline.py` — orchestrator `sync_playlists(sp, selected_playlists, output_root, on_track_progress, on_log, is_cancelled)`. Per playlist: enforce the 15-track cap (process first 15 by Spotify order, log the rest as skipped-due-to-cap — never truncate silently); per track: search → match → download → tag using art bytes already fetched → immediately call `prescale_art.resize_and_crop(art_bytes, size=ART_SIZE)`/`image_to_rgb565_bytes(...)` on those same art bytes to write the `.art` sibling (no redundant re-extraction from the finished MP3). **Pass `size` explicitly** — the reused functions default to `DEFAULT_ART_SIZE = 90` (`prescale_art.py:48`), which would produce 90×90 art on a 240×240 display. The firmware infers the side length from byte count (`side = sqrt(bytes/2)`, validated `≤ ART_MAX_SIDE = 240` in `ui_manager.cpp:220`), so any square ≤240 is valid; use **240** (`ART_SIZE = 240`, matching `ART_MAX_SIDE`) for full-resolution art since PSRAM absorbs the allocation (`config.h:110`).

### Output layout

```
<output_root>/                    (copy this whole tree onto the SD card)
├── <Playlist Name>/
│   ├── 01 - Song Title.mp3
│   ├── 01 - Song Title.art
│   └── ...
```
Folder names = sanitized Spotify playlist names. Sanitization must go beyond stripping `\ / : * ? " < > |`, because both Windows folder creation and the firmware's own scan reject certain names: also (a) strip/replace a leading `.` — `isJunkFolder` (`sd_manager.cpp:96`) skips any folder starting with `.`; (b) reject the literal `All Songs` (case-insensitive) — it collides with the synthesized root entry; (c) handle Windows reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`–`COM9`, `LPT1`–`LPT9`) and trailing dots/spaces (Windows silently strips them); (d) fall back to a stable placeholder (e.g. `Playlist <n>`) if a name sanitizes to empty.

Filenames get a zero-padded Spotify-order track-number prefix (`01 -`, `02 -`) because the firmware sorts by filename-derived title (`sd_manager.cpp:88`), so a numeric prefix is the only way to preserve playlist order on-device. **Accepted tradeoff**: `songTitle` derives the on-screen title from the *whole* filename basename (no ID3-title reading exists), so this prefix is visible in the song list and Now Playing (e.g. "01 - Song Title"). Preserving order requires it; the only way to hide it would be a firmware change to read the `TIT2` tag for the display title, which is out of scope here.

One expectation to surface in the README/UX: the on-device "All Songs" entry scans the SD root recursively but stops at `MAX_SONGS = 15` *total* across the whole card (`scanMusic` → `scanDir`, `sd_manager.cpp:37`), so once more than ~one playlist is synced, "All Songs" shows an arbitrary 15-song subset. Per-folder playlists are the intended way to browse a multi-playlist card; the tool shouldn't imply "All Songs" is a complete index.

### GUI integration

Convert `PseudoVinylConverter` to a `ttk.Notebook` with two tabs: existing art-converter UI moved into its own tab frame, plus a new "Spotify Sync" tab built by a new `SpotifySyncTab` class in `spotify_sync_gui.py` (imports the shared `C` palette / fonts from `prescale_art_gui.py` for visual consistency). Flow: "Link Spotify Account" button (background thread, same pattern as `_conversion_worker`) → browser OAuth → "Linked as <name>"; "Load Playlists" → scrollable checkbox list showing track counts, with an inline amber warning on any playlist row over 15 tracks ("⚠ 23 tracks — only first 15 will sync") and a banner + disabled Start button if more than **7** playlists are checked (the effective on-device folder cap — see limits.py note); "Start Sync" reuses the existing progress-bar/activity-log/cancel-flag pattern, extending the progress label to `Playlist 2/3 · Track 8/15 · "Song Name"`. Every cap-related skip is logged explicitly in the activity log (reusing the existing warning/dim log tags) so the run is auditable.

### Packaging changes

`requirements.txt`: add `spotipy>=2.24.0`, `yt-dlp>=2024.8.6` (loose pin, comment noting yt-dlp needs periodic bumping as YouTube changes break extraction). `PseudoVinylConverter.spec`: `hiddenimports=collect_submodules('yt_dlp')` (yt-dlp lazily imports its extractors — PyInstaller's static analysis won't find them otherwise), new `datas` entries for `spotify_sync/*.py` matching the existing `prescale_art.py` convention, verify `certifi`'s CA bundle is bundled for TLS to `accounts.spotify.com`.

## Staged milestones (each independently reviewable/testable)

1. **M1 — Spotify auth + playlist listing** (no YouTube, no GUI tab): `spotify_config.py`, `spotify_auth.py`, `spotify_client.py`, exercised via a throwaway terminal test script. Verify: login works, token cache survives a second run without a browser popup, playlist/track counts are correct.
2. **M2 — YouTube search + download + tagging for one track** (still no GUI): `youtube_search.py`, `youtube_download.py`, `tagger.py`. Verify against a handful of real tracks including a deliberately tricky one (a song with a prominent "Live"/"Remix" version in search results) — check match quality, tags, embedded art, playable audio.
3. **M3 — Full pipeline + GUI tab**: `limits.py`, `sync_pipeline.py`, `spotify_sync_gui.py`, `ttk.Notebook` refactor of `prescale_art_gui.py`, spec/requirements updates. Verify: full click-through from "Link Spotify" to a completed multi-track sync in the running app.
4. **M4 — `.art` generation wired into the pipeline + cap warnings surfaced in the UI**: row-level and banner-level warnings in `spotify_sync_gui.py`, README updates (ffmpeg prerequisite, one-time Spotify app registration note for forks). Verify: a synced folder copied onto a real (or SD-card-shaped test) directory is read correctly by the firmware; oversized selections show warnings rather than silently dropping tracks.

## Critical files

- `tools/prescale_art/prescale_art_gui.py` — gets the `ttk.Notebook` refactor; source of the `C` palette and threading/logging patterns to reuse.
- `tools/prescale_art/prescale_art.py` — reused directly for `.art` generation (`resize_and_crop`, `image_to_rgb565_bytes`).
- `tools/prescale_art/PseudoVinylConverter.spec`, `tools/prescale_art/requirements.txt` — packaging updates.
- `firmware/src/storage/sd_manager.cpp`, `firmware/src/config.h` — authoritative source of the folder-playlist convention and the `MAX_SONGS`/`MAX_PLAYLISTS` caps the tool must respect and mirror.

## Verification

- M1/M2 are pure-Python and verified from a terminal before any GUI risk (login flow, playlist fetch, single-track search/download/tag correctness) — no hardware needed.
- M3 verified by running the actual GUI app (`run.bat` or `python prescale_art_gui.py`) end-to-end: link account, select playlists, sync, watch progress/log.
- M4 verified by copying a synced output folder onto a real SD card (or a directory formatted the same way) and confirming on real hardware (or by re-reading `sd_manager.cpp`'s scan logic) that playlists, songs, and art all appear correctly, and that an intentionally oversized playlist/selection shows the warning instead of silently losing tracks.
