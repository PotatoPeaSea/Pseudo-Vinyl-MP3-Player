# Pseudo Vinyl MP3 Player — User Guide

Everything you need to load music, pair your earbuds, and use the player day-to-day. For build instructions and internals, see the [README](../README.md); for hardware wiring, see the [README's Wiring section](../README.md#wiring) (the authoritative source is `firmware/src/config.h` — the [PRD](PRD.md) wiring table predates the current board and is out of date).

---

## 1. What You Need

- The player (or a breadboard prototype), charged or on USB power
- A **microSD card, formatted FAT32** (up to 32 GB)
- MP3 files (44.1 kHz recommended — see [Tips](#6-tips--limitations))
- Bluetooth earbuds or a speaker — audio output is Bluetooth-only, there's no wired jack
- A PC with Python for preparing album art (optional but recommended)

---

## 2. Preparing the SD Card

1. **Format** the card as FAT32.
2. **Prepare album art on your PC** (optional). The player can't decode JPEG — art is pre-converted to a device-native format:

   ```bash
   cd tools/prescale_art
   pip install -r requirements.txt
   python prescale_art.py "D:\My Music"
   ```

   For each `song.mp3` with embedded cover art this creates a small `song.art` file next to it. Prefer clicking to typing? Run `prescale_art_gui.py` instead and drop your music folder onto the window.

3. **Copy your music** (the `.mp3` *and* `.art` files together) onto the card. Any folder layout works — the player scans all folders. **Top-level folders become playlists**: put an album or mood in its own folder (e.g. `/Road Trip/song.mp3`) and it shows up as "Road Trip" on the Playlists screen. Loose files at the card's root, and everything nested inside every folder, are always browsable together under "All Songs".
4. Insert the card **before powering on**. The library (up to 15 songs) and playlist list load automatically at boot — no need to connect a speaker first.

> **Upgrading from an older version?** Art files made before July 2026 may be 240×240 or 120×120 — both work directly with the current firmware, no regeneration needed.

---

## 3. Controls

Three buttons and a rotary encoder. What they do depends on which screen you're on:

### On the Now Playing screen

| Control | Action |
|---|---|
| **Play button** (short press) | Play / pause |
| **Next button** | Next track |
| **Previous button** | Restart track (if >3s in), otherwise previous track |
| **Encoder — rotate** | Volume up / down |
| **Encoder — press** | Cycle play mode: Normal → Shuffle → Repeat All → Repeat One |

### In menus (Library, Playlists, Settings, Bluetooth)

| Control | Action |
|---|---|
| **Encoder — rotate** | Move the highlight up / down the list |
| **Play button** | Select the highlighted item |
| **Next / Previous buttons** | Switch between screens |
| **Encoder — press** | Go back |

### Screen order

`Next`/`Previous` cycle through the screens in this order:

```
Library  →  Playlists  →  Settings  →  Bluetooth  →  Now Playing  →  (back to Library)
```

---

## 4. The Screens

### 🎵 Library (start screen)
A scrollable list of MP3s (up to 15), sorted alphabetically. This is "All Songs" — the currently active playlist, which defaults to everything on the card. Songs with album art show a picture icon. Rotate the encoder to browse, press **Play** to start a song — you'll jump to Now Playing.

### 🗂 Playlists
One entry per top-level folder on the SD card, plus "All Songs" at the top (always available, even with no folders). Selecting a playlist rescans that folder and swaps it into the Library screen — the currently playing song stops if it belonged to a different playlist.

### 💿 Now Playing
The signature screen: your album art spins like a record at the center, with a gold progress ring around the display edge. The song title scrolls below. Top corners show the play mode (left) and volume (right); a small Bluetooth icon appears top-center when connected.

### ⚙ Settings
- **Output: Bluetooth** — informational only; Bluetooth is the only output on this board, so there's nothing to select here.
- **Playlists** — shortcut to the Playlists screen.
- **Bluetooth Devices** — shortcut to the Bluetooth screen.

### 🔵 Bluetooth
Shows connection status and a live list of discovered audio devices. See the next section.

---

## 5. Pairing Bluetooth Earbuds

1. Put your earbuds/speaker in **pairing mode**.
2. Go to the **Bluetooth** screen. The player scans continuously — nearby devices appear in the list within a few seconds ("Searching…" shows while the list is empty).
3. Rotate the encoder to highlight your device and press **Play** to select it. The status line shows "Connecting…", then "Connected".
4. Play a song. Audio now streams to your earbuds.

**Auto-reconnect:** the player remembers your device and reconnects to it automatically on every boot — you only pair once. To switch to different earbuds, just pick another device from the Bluetooth list.

**No sink connected?** Playback politely waits (it won't silently burn through your playlist) until a device connects.

---

## 6. Tips & Limitations

- **Use 44.1 kHz MP3s.** The Bluetooth link runs at a fixed 44.1 kHz; files at 48 kHz or other rates will play slightly fast/slow. Most music files are already 44.1 kHz.
- **Song length shows "0:00" for the first second or two of a track**, then settles on a number and stays there. It's estimated from how much of the compressed file has been read so far (not read from the file's tags), computed once early in the track and held fixed afterward rather than continuously refined.
- **Volume is remembered**, as is your paired device.
- **Album art without the tool:** songs without an `.art` file show a gold record label instead — everything else works normally.
- **Library is capped at 15 songs at a time** (per playlist) — put more music in folders and use the Playlists screen to switch between sets rather than expecting one giant list.

---

## 7. Charging & Power

- Charge via the **USB-C** port (TP4056, ~1A). Charging works while powered off.
- The **slide switch** hard-disconnects the battery — use it for storage or transport.
- Battery life target is ≥4 hours of Bluetooth playback (depends on the cell fitted).

---

## 8. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| **"No SD card!" on boot / empty Library** | Card not FAT32, not inserted fully, or inserted after boot. Power-cycle with the card in. |
| **Board won't boot at all with SD module wired** | If your SD module has a pull-up wired to GPIO 12, it must not be connected there. See the wiring notes in the [README](../README.md#wiring) (`firmware/src/config.h` is the authoritative pin source). |
| **My earbuds never appear in the list** | Make sure they're in *pairing* mode (not just on), and close to the player. Some devices only advertise for ~60s — re-enter pairing mode. |
| **Connected, but no sound** | Check a song is actually playing (Now Playing shows the spinning record) and volume isn't at 0. |
| **Music sounds too fast/slow** | The file isn't 44.1 kHz — re-encode it to 44.1 kHz. |
| **Songs play but art shows a plain gold label** | No `.art` file next to the MP3, or the art file is larger than 240×240 / corrupt. Re-run the pre-scaler tool (`--force` to regenerate); check the serial log for the exact reason. |
| **Playback "frozen"** | No Bluetooth sink connected — playback holds until your earbuds connect. |
| **Wrong/garbled colors on screen** | SPI wiring issue on the display (check MOSI/SCLK/DC against `config.h`), or drop `SPI_FREQUENCY` from 40MHz to 27MHz — both display and SD are GPIO-matrix routed on this board, which makes 40MHz more marginal than an IOMUX-native pin would be. |
| **Volume knob scrolls instead of changing volume** | You're on a menu screen — volume control is on Now Playing only. |

Still stuck? Connect USB and open a serial monitor at **115200 baud** — the firmware logs every boot step (`[SD]`, `[BT]`, `[Audio]`, `[UI]`) and most failures are named explicitly there.
