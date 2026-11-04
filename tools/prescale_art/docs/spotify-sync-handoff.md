# Spotify Sync — Implementation Handoff

**Status:** Code-complete for M1–M4. Verified as far as possible without a
live Spotify Client ID or ffmpeg. Three things need a human with a display +
credentials to close out (see "Remaining to verify").

**Design doc:** [spotify-sync-plan.md](spotify-sync-plan.md) — read that first
for the *why*. This doc is the *what's built / what's left*.

---

## What this feature does

Adds a **Spotify Sync** tab to the existing desktop tool. The user links their
Spotify account, picks playlists, and the tool assembles ready-to-copy
SD-card folders: for each track it finds the audio on YouTube (`yt-dlp`),
downloads an MP3, writes ID3 tags, and generates the matching `.art` sibling —
removing the manual "rip + drag onto SD" step. Spotify is used only for
playlist/track metadata and album art (its API can't serve audio).

---

## Files

### New — `spotify_sync/` subpackage (pure logic, no GUI)
| File | Role |
|---|---|
| `spotify_config.py` | Client ID, redirect URI, scopes, token-cache path. **Placeholder Client ID — must be set.** |
| `spotify_auth.py` | PKCE browser login via loopback `HTTPServer`; `login()`, `get_client()`, `is_linked()`, `unlink()`. |
| `spotify_client.py` | `list_user_playlists()`, `get_playlist_tracks()`; `PlaylistSummary`/`TrackInfo` dataclasses. |
| `youtube_search.py` | Query build, `ytsearch` metadata-only candidates, duration-gate + fuzzy scoring, `find_match()`. |
| `youtube_download.py` | `download_audio()` (yt-dlp + ffmpeg MP3 postproc); `FfmpegMissingError` / `DownloadError`. |
| `tagger.py` | `fetch_art_bytes()`, `write_tags()` (TIT2/TPE1/TALB/APIC — first tag-*writing* in the project). |
| `naming.py` | Filesystem-safe `sanitize_playlist_name`, `sanitize_song_title`, `track_filename`. |
| `limits.py` | `MAX_SONGS_PER_PLAYLIST=15`, `MAX_FOLDER_PLAYLISTS=7`, `ART_SIZE=240` — mirror of firmware caps. |
| `sync_pipeline.py` | `sync_playlists(...)` orchestrator: search→download→tag→`.art`, all cap enforcement + logging. |

### New — GUI + test
- `spotify_sync_gui.py` — `SpotifySyncTab` class (link → load playlists → pick → sync; own progress bar + log).
- `spotify_sync_m1_test.py` — throwaway terminal script to verify login + playlist fetch.

### Modified
- `prescale_art_gui.py` — refactored into a `ttk.Notebook`: **Album Art** tab (unchanged behaviour, moved into `_build_art_tab()`) + **Spotify Sync** tab. Added `_style_notebook()`. Header subtitle → "SD-Card Music Toolkit".
- `requirements.txt` — added `spotipy`, `requests`, `yt-dlp`.
- `PseudoVinylConverter.spec` — `collect_submodules('yt_dlp')`, certifi CA bundle, `datas` for every `spotify_sync/*.py` + `spotify_sync_gui.py`.
- `README.md` — Spotify Sync section (ffmpeg + Spotify app setup, caps, on-device notes).
- `docs/spotify-sync-plan.md` — the saved plan (with the `naming.py` note).

---

## Key design decisions (and why)

- **7-folder cap, not 8.** Firmware `scanPlaylists()` seeds the list with a
  synthesized "All Songs" entry, so only 7 real folders show on-device — and
  it drops extras in non-deterministic FAT order. The tool enforces 7 upstream
  (banner + disabled Start). See `limits.py` comment.
- **`.art` at 240px, not the tool's default 90.** Firmware infers art side
  from byte count (`sqrt(bytes/2)`, ≤240). We pass `size=ART_SIZE=240`
  explicitly in `sync_pipeline._write_art_file` for full display resolution.
- **Art bytes fetched once**, reused for both the APIC tag and the `.art`
  file — no re-extraction from the finished MP3.
- **Match quality = duration gate first, then fuzzy score.** A candidate is
  hard-rejected if its length differs from Spotify's by >max(12s, 8%); "live/
  remix/cover" terms are *penalized* (not banned) unless the Spotify track
  itself is that kind. Below a score floor → "no match", logged + skipped.
- **PKCE, we drive the browser.** `spotipy`'s built-in flow blocks on the
  console; we run our own loopback `HTTPServer` and use `SpotifyPKCE` only as a
  token/cache manager. Redirect uses `127.0.0.1` (Spotify rejects `localhost`).
- **`naming.py` split out** from the pipeline so the sanitization edge cases
  are unit-testable — the one deviation from the plan's file list.

---

## Verified ✓

- All 13 Python files compile; `spotify_sync` imports; `prescale_art` reuse resolves.
- **Live YouTube matching** (real network): "Get Lucky" → correct 369s studio
  version (radio edits gated out); "Bohemian Rhapsody" → studio 1.00, Wembley
  live down-ranked to 0.55. The tricky live/remix case works.
- **Naming edge cases**: `.hidden`→`hidden`, `All Songs`→`All Songs (playlist)`,
  reserved `NUL`/`CON` guarded, illegal chars stripped, trailing dots/empty → fallback.
- **Full app builds headlessly** with both tabs; clean teardown.
- **Cap gating**: Start enabled at 1–7 selected, disabled at 8+ (banner shown),
  disabled with no output folder.

---

## Remaining to verify (needs human)

1. **Spotify login end-to-end** — *blocked on credentials.*
   - Register an app at https://developer.spotify.com/dashboard
   - Add redirect URI **exactly**: `http://127.0.0.1:43813/callback`
   - Put the Client ID in `spotify_sync/spotify_config.py` (`SPOTIFY_CLIENT_ID`).
   - From `tools/prescale_art/`: `python spotify_sync_m1_test.py`
     — 1st run opens a browser; **2nd run must link silently** (token cache).
     Confirm playlist/track counts look right.

2. **Real download + tag + `.art`** — *blocked on ffmpeg.*
   - Install ffmpeg on PATH (`winget install Gyan.FFmpeg`); `ffmpeg -version`.
   - Run the Sync tab against a small real playlist. Check: playable MP3,
     correct tags/embedded art, a `.art` sibling of `240*240*2 = 115200` bytes,
     and that a deliberately-tricky track (has a prominent live/remix on YT)
     matched the right version.

3. **Visual pass of the tabbed GUI** on a real display — tab styling, playlist
   checklist scroll, progress label `Playlist x/y · Track a/b · phase · "name"`.

4. **On-device**: copy an output folder to a real SD card (or SD-shaped dir);
   confirm the firmware lists the playlists/songs/art and that order is
   preserved by the `NN - ` filename prefix.

---

## Gotchas / notes for the next session

- **Circular import** is intentional and handled: `spotify_sync_gui` imports the
  palette from `prescale_art_gui`, which imports `SpotifySyncTab` *lazily* inside
  `_build_ui`. If the optional deps are missing, the tab degrades to an error
  message instead of breaking the app.
- **Pre-existing headless artifact**: forcing `<Configure>` on a withdrawn root
  makes the old `_update_background` (wood texture) throw "image doesn't exist".
  Not a regression from this work — it's the existing resize handler and is
  harmless in real windowed use.
- **yt-dlp rots.** When YouTube changes break extraction, `pip install -U yt-dlp`.
  The pin in `requirements.txt` is deliberately loose.
- **ID3 tags are for desktop players, not the device.** Firmware derives the
  on-screen title from the *filename* (hence the `NN - ` prefix is visible
  on-screen). Changing that would need a firmware change to read TIT2 — out of scope.
- Token cache lives at `%APPDATA%\PseudoVinylConverter\spotify_token_cache.json`.
  Delete it (or call `spotify_auth.unlink()`) to force re-login.

---

## Quick start for whoever picks this up

```bash
cd tools/prescale_art
pip install -r requirements.txt          # spotipy, yt-dlp, requests, etc.
# 1. set SPOTIFY_CLIENT_ID in spotify_sync/spotify_config.py
# 2. install ffmpeg, confirm `ffmpeg -version`
python spotify_sync_m1_test.py           # verify auth + playlist fetch
python prescale_art_gui.py               # full GUI: Spotify Sync tab
```
