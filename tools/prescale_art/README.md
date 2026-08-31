# Album Art Pre-Scaler

Extracts embedded album art from MP3 files, resizes to 240×240 pixels, and converts to **RGB565 raw bitmap** (`.art`) for direct loading on the ESP32-S3 GC9A01 circular display.

## Setup

```bash
cd tools/prescale_art
pip install -r requirements.txt
```

## Usage

```bash
# Basic — process all MP3s in a folder (recursive), output RGB565 .art files
python prescale_art.py /path/to/music

# Force re-process everything (ignore existing .art files)
python prescale_art.py /path/to/music --force

# Output as compressed JPEG instead of raw RGB565
python prescale_art.py /path/to/music --format jpeg --quality 85
```

## Output Formats

| Format | Extension | Size per file | ESP32 Load Time | Notes |
|---|---|---|---|---|
| **RGB565** (default) | `.art` | 115,200 bytes | Fast (no decode) | Raw pixel data, direct DMA to display |
| **JPEG** | `.art.jpg` | ~5-15 KB | Slower (decode needed) | Saves SD card space, requires JPEG decoder on device |

## How It Works

1. Recursively finds all `.mp3` files
2. Extracts the first embedded `APIC` (album art) frame from ID3v2 tags
3. Center-crops to square, then resizes to 240×240 using Lanczos resampling
4. Converts to RGB565 (16-bit, big-endian — matches GC9A01 native byte order)
5. Saves as `<songname>.art` alongside the original MP3
6. Skips files where the `.art` is already newer than the `.mp3`

## File Placement

Place your `.art` files on the SD card **alongside** the MP3s:

```
SD Card/
├── Artist/
│   ├── song1.mp3
│   ├── song1.art      ← generated
│   ├── song2.mp3
│   └── song2.art      ← generated
```

The firmware will look for `<filename>.art` when loading album art for playback.

---

# Spotify Sync (GUI)

The desktop app (`python prescale_art_gui.py` or the packaged
`PseudoVinylConverter` exe) has two tabs:

- **Album Art** — the classic drag-a-folder art pre-scaler described above.
- **Spotify Sync** — link your Spotify account, pick playlists, and the tool
  assembles ready-to-copy SD-card folders: it finds each track's audio on
  YouTube, downloads it as MP3, tags it, and writes the matching `.art`
  sibling — no manual sourcing.

> Spotify's API can't legally serve audio, so audio is sourced from YouTube
> via `yt-dlp`. This is intended for personal/family use with music you're
> entitled to. Respect YouTube's and Spotify's terms of service.

## Prerequisites

- **Python deps**: `pip install -r requirements.txt` (adds `spotipy`,
  `yt-dlp`, `requests`).
- **ffmpeg** on your `PATH` — required to extract MP3 audio. It is *not*
  bundled. Install it (Windows: `winget install Gyan.FFmpeg` or grab a build
  from https://www.gyan.dev/ffmpeg/builds/) and confirm `ffmpeg -version`
  works in a new terminal. The Sync tab reports a clear error if it's missing.
- **A Spotify Client ID** — see below.

## One-time Spotify app setup (required, incl. forks)

The Sync tab authenticates with a Spotify Developer app using the PKCE flow
(no client secret). You must supply a Client ID:

1. Create an app at https://developer.spotify.com/dashboard
2. Under **Redirect URIs**, add exactly:
   `http://127.0.0.1:43813/callback`
   (Spotify requires the loopback literal `127.0.0.1` — not `localhost`.)
3. Copy the app's **Client ID** into `spotify_sync/spotify_config.py`
   (`SPOTIFY_CLIENT_ID`).

Login opens your browser once; the token is cached under
`%APPDATA%\PseudoVinylConverter\` so subsequent runs link silently.

## Device caps the tool enforces

These mirror the firmware's hard RAM limits (`firmware/src/config.h`) — the
tool warns and refuses to exceed them rather than producing a card the device
would truncate unpredictably:

- **15 songs per playlist folder.** A larger playlist syncs its first 15 (in
  Spotify order); the rest are logged as skipped, never dropped silently. A
  row-level `⚠ only first 15 will sync` warning shows in the picker.
- **7 playlist folders.** The firmware's playlist limit is 8, but one slot is
  the synthesized **All Songs** entry, so only 7 real folders appear
  on-device. Selecting more than 7 shows a banner and disables **Start**.

## Notes on the on-device result

- Track filenames are prefixed with a zero-padded number (`01 - Title.mp3`) to
  preserve playlist order — the firmware sorts by filename, so this prefix is
  visible in the song title on-screen. It's the only way to keep order.
- The device's **All Songs** view is capped at 15 songs *total* across the
  whole card, so once you've synced more than about one playlist it shows an
  arbitrary subset — browse per-folder playlists instead.
