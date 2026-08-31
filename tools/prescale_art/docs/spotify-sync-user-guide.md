# Spotify Sync — User Guide

This guide walks you through using the **Spotify Sync** feature of the Pseudo
Vinyl Converter, from first-time setup to copying finished playlists onto your
SD card. No programming needed once setup is done.

> **What it does, in one sentence:** you link your Spotify account, tick the
> playlists you want, and the tool builds ready-to-copy SD-card folders — each
> song downloaded as an MP3 (audio sourced from YouTube), tagged, and paired
> with its album-art `.art` file for the round display.

> **A note on how it sources audio.** Spotify's API is not allowed to hand out
> audio files, so the tool uses Spotify only for your playlists, track names,
> and album art. The actual audio is found and downloaded from YouTube with
> `yt-dlp`. This is meant for personal/family use with music you're entitled
> to; please respect YouTube's and Spotify's terms of service.

---

## Contents

1. [One-time setup](#1-one-time-setup)
2. [Where the API key goes](#2-where-the-api-key-goes)
3. [Launching the app](#3-launching-the-app)
4. [The interface, control by control](#4-the-interface-control-by-control)
5. [Syncing a playlist, step by step](#5-syncing-a-playlist-step-by-step)
6. [Understanding the limits and warnings](#6-understanding-the-limits-and-warnings)
7. [Copying the result to your SD card](#7-copying-the-result-to-your-sd-card)
8. [Troubleshooting](#8-troubleshooting)
9. [Privacy, accounts, and unlinking](#9-privacy-accounts-and-unlinking)

---

## 1. One-time setup

You need three things installed once. After this, day-to-day use is just
opening the app and clicking.

### a) Python and the tool's dependencies

You need **Python 3.8 or newer** ([python.org/downloads](https://www.python.org/downloads/)).
During install, tick **"Add Python to PATH."**

Then install the tool's dependencies. Open a terminal in the
`tools/prescale_art` folder and run:

```bash
pip install -r requirements.txt
```

This adds `spotipy` (Spotify), `yt-dlp` (YouTube), `requests`, plus the
image/tag libraries.

> **Optional but tidy — use a virtual environment.** This keeps the tool's
> packages separate from the rest of your system:
>
> ```bash
> cd tools/prescale_art
> python -m venv .venv
> .venv\Scripts\activate         # Windows (PowerShell/cmd)
> # source .venv/bin/activate    # macOS/Linux
> pip install -r requirements.txt
> ```
>
> If you use a venv, activate it (the `activate` line) each time before
> launching the app from a terminal. The double-click `run.bat` launcher does
> **not** use the venv — it installs into your system Python instead, so pick
> one approach and stick with it.

### b) ffmpeg (required for downloading audio)

`ffmpeg` is a free tool that converts the downloaded audio into MP3. It is
**not** bundled — you install it once, system-wide.

- **Windows:** `winget install Gyan.FFmpeg` in a terminal, **or** download a
  build from <https://www.gyan.dev/ffmpeg/builds/> and add its `bin` folder to
  your PATH.
- **macOS:** `brew install ffmpeg`
- **Linux:** `sudo apt install ffmpeg` (or your distro's equivalent)

**Confirm it worked** by opening a *new* terminal and running:

```bash
ffmpeg -version
```

If you see version text, you're set. If you see "command not found," ffmpeg
isn't on your PATH yet — the Sync tab will show a clear error and refuse to
download until this is fixed.

### c) A Spotify Client ID

This is the "API key" the tool uses to read your playlists. It's free and
takes about two minutes to create. See the next section.

---

## 2. Where the API key goes

The Sync tab logs into Spotify through a **Spotify Developer app** that you
register. It uses the PKCE flow, which means **there is no secret key to
protect** — only a public **Client ID**.

### Create the app

1. Go to the [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
   and log in with your normal Spotify account.
2. Click **Create app**. Give it any name and description (e.g. "Pseudo Vinyl
   Sync"). For the website you can put anything.
3. In **Redirect URIs**, add this line **exactly** and save:

   ```
   http://127.0.0.1:43813/callback
   ```

   > This must be typed precisely. Spotify requires the literal loopback
   > address `127.0.0.1` — `localhost` will be rejected. The port `43813` and
   > the `/callback` path must match too.
4. Open the app's **Settings** and copy its **Client ID**.

### Paste the Client ID into the tool

Open this file in a text editor:

```
tools/prescale_art/spotify_sync/spotify_config.py
```

Near the top, find this line:

```python
SPOTIFY_CLIENT_ID = "REPLACE_WITH_YOUR_SPOTIFY_CLIENT_ID"
```

Replace the placeholder text with your Client ID (keep the quotes):

```python
SPOTIFY_CLIENT_ID = "a1b2c3d4e5f6...your actual id..."
```

Save the file. That's the only edit you need. Until this is set, the **Link
Spotify Account** button reports that no Client ID is configured.

> **Why is it safe to paste this into a source file?** With PKCE there is no
> client *secret*. The Client ID just identifies the app; it can't be used to
> access your account without you completing the browser login. At
> personal/family scale, one shared Client ID for everyone is fine. (If you
> ever fork/share the project, each maintainer can register their own app and
> swap this one line — the instructions are also in the file's header.)

---

## 3. Launching the app

Any of these opens the same two-tab window:

- **Double-click `run.bat`** (Windows) — checks Python, installs dependencies,
  and launches. Easiest for non-technical use.
- **From a terminal:** `python prescale_art_gui.py` (from `tools/prescale_art`).
  Use this if you set up a venv — activate it first.
- **The packaged `PseudoVinylConverter` exe**, if you built one.

The window has two tabs across the top:

- **Album Art** — the original tool: drag a folder of MP3s you already have and
  it generates `.art` files. Unchanged.
- **Spotify Sync** — the subject of this guide.

Click **Spotify Sync**.

---

## 4. The interface, control by control

Here's the Spotify Sync tab top to bottom:

```
  Spotify Sync
  Link Spotify, pick playlists, and press to assemble SD-ready folders.

  [ Link Spotify Account ]   Not linked            ← link row + status
  [ Load My Playlists ]                             ← disabled until linked

  ┌─────────────────────────────────────────────┐
  │ ☐ My Road Trip Mix           14 tracks        │  ← scrollable playlist
  │ ☐ Focus                23 tracks  ⚠ only...    │     checklist
  │ ☐ Party                 9 tracks               │
  └─────────────────────────────────────────────┘

  ⚠ 8 playlists selected — the device shows at most 7 folders…   ← banner
                                                       (only when over cap)

  Output:  [ C:\Users\you\SD-staging          ] [ Browse ]

        [   ♫  Press to Cut the Record  ♫   ]      ← Start (disabled until ready)

  ▬▬▬▬▬▬▬▬▬▬░░░░░░░░░░░░░░░░░░░░                    ← progress bar
  Playlist 1/2 · Track 8/15 · downloading · "Song Name"   ← progress label

  ┌─────────────────────────────────────────────┐
  │ ▶ Focus                                       │  ← activity log
  │   ✓ 01 - Some Song                            │     (colour-coded)
  │   ⚠ No match found for … — skipped            │
  └─────────────────────────────────────────────┘
```

| Control | What it does |
|---|---|
| **Link Spotify Account** | Opens your browser to log in to Spotify. After linking it becomes **Re-link / Switch Account**. |
| **link status** (text beside the button) | Shows `Not linked`, `Opening browser…`, or `Linked as <your name>` in green when connected. |
| **Load My Playlists** | Enabled once linked. Fetches all your playlists (including private/collaborative) into the checklist. |
| **Playlist checklist** | Scrollable list of your playlists with track counts. Tick the ones to sync. A playlist over 15 tracks shows an amber `⚠ only first 15 will sync`. |
| **Over-cap banner** | Appears in red only if you tick more than 7 playlists, telling you how many to uncheck. |
| **Output** field + **Browse** | The folder where the finished playlist folders are written. This is your SD-card staging area. |
| **Press to Cut the Record** (Start) | Stays disabled until you're linked, have ticked 1–7 playlists, and chosen an output folder. Click to begin. While running it turns red and reads **Stop the Press** — click again to stop after the current track. |
| **Progress bar + label** | Live position: `Playlist x/y · Track a/b · <phase> · "title"`, where phase is searching / downloading / tagging / art. |
| **Activity log** | A colour-coded running report: green ✓ for each finished track, amber ⚠ for skips (no match, over-cap), red ✗ for errors, plus a per-playlist summary at the end. |

---

## 5. Syncing a playlist, step by step

1. **Link your account.** Click **Link Spotify Account**. Your browser opens a
   Spotify login/permission page. Approve it — the page will say *"Spotify
   linked, you can close this tab."* Back in the app the status turns green:
   *Linked as \<your name\>.*

   > You only log in once. The tool caches your login token, so next time it
   > links silently without opening the browser.

2. **Load your playlists.** Click **Load My Playlists**. Your playlists appear
   in the checklist with their track counts.

3. **Tick what you want.** Check up to **7** playlists. Watch for warnings:
   - A row saying `⚠ only first 15 will sync` means that playlist is longer than
     the device's 15-song limit — only its first 15 tracks (in Spotify order)
     will be included.
   - A red banner means you've ticked more than 7 playlists; untick some until
     it disappears.

4. **Choose an output folder.** Click **Browse** and pick an empty folder to
   stage the results (e.g. a folder on your desktop, or the SD card itself).

5. **Press to Cut the Record.** Once the button is enabled, click it. The tool
   works through each playlist and track:
   - searches YouTube for the best-matching audio,
   - downloads it and converts to MP3 (this is the ffmpeg step),
   - writes the song's title/artist/album tags and embeds the album art,
   - generates the matching `.art` file for the round display.

   The progress bar and log update live. You can click **Stop the Press** to
   halt cleanly after the current track.

6. **Read the summary.** When it finishes, the log prints a per-folder tally,
   e.g. `Road Trip Mix/ — 14 synced, 0 no-match, 0 failed, 0 over-cap`, and the
   path to your finished output.

---

## 6. Understanding the limits and warnings

These caps come from the ESP32's fixed RAM budget in the firmware. The tool
enforces them *up front* rather than letting the device silently drop things,
so what you see in the app is what the device will show.

| Limit | Value | What the tool does |
|---|---|---|
| Songs per playlist folder | **15** | Syncs the first 15 in Spotify order; logs the rest as skipped (never silently dropped). Row warning: `⚠ only first 15 will sync`. |
| Playlist folders on the card | **7** | Ticking more than 7 shows a red banner and disables **Start**. (The firmware's real limit is 8, but one slot is always the automatic **All Songs** entry, leaving 7 for your folders.) |

**About the device's "All Songs" view:** the player shows an automatic *All
Songs* entry alongside your folders, but it lists at most 15 songs *total*
across the whole card. Once you've synced more than roughly one playlist's
worth, All Songs shows only an arbitrary subset — browse your named playlist
folders instead. It's not a complete index of the card.

**Track order and the filename prefix:** each song is saved as
`01 - Title.mp3`, `02 - Title.mp3`, etc. The number keeps your playlist order
on the device (the firmware sorts by filename). Because the on-screen title
comes from the filename, you'll see that `01 - ` prefix in the song list and
Now Playing screen — it's the trade-off for preserving order.

---

## 7. Copying the result to your SD card

Your output folder looks like this:

```
<output folder>/
├── Road Trip Mix/
│   ├── 01 - Song One.mp3
│   ├── 01 - Song One.art
│   ├── 02 - Song Two.mp3
│   ├── 02 - Song Two.art
│   └── …
└── Focus/
    └── …
```

Copy the **contents** of the output folder (the playlist folders themselves)
onto the **root of the SD card**. Each playlist folder becomes one browsable
playlist on the device, in the order you'd expect. The `.mp3` and its `.art`
sibling must stay together in the same folder — the firmware looks for
`<songname>.art` next to each song.

Insert the card into the player and power on; your playlists appear in the
menu.

---

## 8. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| **Link button says "No Spotify Client ID configured"** | You haven't set `SPOTIFY_CLIENT_ID` in `spotify_sync/spotify_config.py`. See [section 2](#2-where-the-api-key-goes). |
| **Browser opens but login fails / "redirect URI mismatch"** | The redirect URI in your Spotify app must be exactly `http://127.0.0.1:43813/callback` — check for typos, `localhost` instead of `127.0.0.1`, or a wrong port. |
| **"Could not start the local login server on port 43813"** | Another copy of the app (or something else) is using that port. Close the other window and retry. |
| **Login "timed out after 120s"** | The browser flow wasn't completed in time. Click **Link Spotify Account** again and finish logging in promptly. |
| **Sync stops immediately with an ffmpeg error** | ffmpeg isn't installed or isn't on your PATH. Install it ([section 1b](#a-python-and-the-tools-dependencies)) and confirm `ffmpeg -version` works in a *new* terminal. |
| **A track is skipped: "No match found"** | No YouTube result passed the quality checks (duration + title match), so it was skipped rather than downloading the wrong version. The rest of the playlist continues. |
| **Downloads suddenly fail for everything** | YouTube changed something and `yt-dlp` needs updating. Run `pip install -U yt-dlp` and try again. |
| **Wrong version downloaded (a live/remix cut)** | Rare — the matcher penalizes live/remix/cover results, but isn't perfect. You can replace that one MP3 manually and regenerate its `.art` from the Album Art tab. |
| **Start button stays greyed out** | You need all three: linked account, 1–7 playlists ticked (banner gone), and an output folder chosen. |

---

## 9. Privacy, accounts, and unlinking

- **Scopes:** the tool requests only *read* access to your playlists
  (`playlist-read-private`, `playlist-read-collaborative`). It never modifies
  your Spotify account or library.
- **Where your login is stored:** the OAuth token is cached on your own machine
  at `%APPDATA%\PseudoVinylConverter\spotify_token_cache.json` (Windows), or
  `~/.pseudovinyl/` on other systems. Nothing is uploaded anywhere.
- **Switching accounts / logging out:** click **Re-link / Switch Account** to
  log in as someone else. To fully log out, delete the token-cache file above
  (that forces a fresh browser login next time).

---

*Related docs: [README.md](../README.md) (quick reference),
[spotify-sync-plan.md](spotify-sync-plan.md) (design rationale),
[spotify-sync-handoff.md](spotify-sync-handoff.md) (implementation status).*
