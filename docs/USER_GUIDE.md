# Pseudo Vinyl MP3 Player — User Guide

Everything you need to load music, pair your earbuds, and use the player day-to-day. For build instructions and internals, see the [README](../README.md); for hardware wiring, see the [PRD](PRD.md).

---

## 1. What You Need

- The player (or a breadboard prototype), charged or on USB power
- A **microSD card, formatted FAT32** (up to 32 GB)
- MP3 files (44.1 kHz recommended — see [Tips](#7-tips--limitations))
- Bluetooth earbuds/speaker, **or** wired headphones (3.5mm) if using the DAC output
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

3. **Copy your music** (the `.mp3` *and* `.art` files together) onto the card. Any folder layout works — the player scans all folders.
4. Insert the card **before powering on**.

> **Upgrading from an older version?** Art files made before July 2026 were 240×240 (and briefly 120×120) and are ignored by the current firmware. Re-run the tool with `--force` to regenerate them at the current 90×90 size.

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

### In menus (Library, Settings, Bluetooth)

| Control | Action |
|---|---|
| **Encoder — rotate** | Move the highlight up / down the list |
| **Play button** | Select the highlighted item |
| **Next / Previous buttons** | Switch between screens |
| **Encoder — press** | Go back |

### Screen order

`Next`/`Previous` cycle through the screens in this order:

```
Library  →  Settings  →  Bluetooth  →  Now Playing  →  (back to Library)
```

---

## 4. The Screens

### 🎵 Library (start screen)
A scrollable list of every MP3 found on the card, sorted alphabetically. Songs with album art show a picture icon. Rotate the encoder to browse, press **Play** to start a song — you'll jump to Now Playing.

### 💿 Now Playing
The signature screen: your album art spins like a record at the center, with a gold progress ring around the display edge. The song title scrolls below. Top corners show the play mode (left) and volume (right); a small Bluetooth icon appears top-center when connected.

### ⚙ Settings
- **Output: Bluetooth / Wired (3.5mm)** — select to toggle where audio goes. Takes effect immediately (the current song restarts on the new output) and is remembered across power-offs.
- **Bluetooth Devices** — shortcut to the Bluetooth screen.

### 🔵 Bluetooth
Shows connection status and a live list of discovered audio devices. See the next section.

---

## 5. Pairing Bluetooth Earbuds

1. Make sure **Output: Bluetooth** is set in Settings (it's the factory default).
2. Put your earbuds/speaker in **pairing mode**.
3. Go to the **Bluetooth** screen. The player scans continuously — nearby devices appear in the list within a few seconds ("Searching…" shows while the list is empty).
4. Rotate the encoder to highlight your device and press **Play** to select it. The status line shows "Connecting…", then "Connected".
5. Play a song. Audio now streams to your earbuds.

**Auto-reconnect:** the player remembers your device and reconnects to it automatically on every boot — you only pair once. To switch to different earbuds, just pick another device from the Bluetooth list.

**No sink connected?** In Bluetooth mode, playback politely waits (it won't silently burn through your playlist) until a device connects.

---

## 6. Wired Listening

1. Plug headphones into the 3.5mm jack.
2. Settings → **Output** → select until it reads **Wired (3.5mm)**.

Bluetooth switches off entirely in wired mode (saves battery). Switch back the same way.

---

## 7. Tips & Limitations

- **Use 44.1 kHz MP3s for Bluetooth.** The BT link runs at a fixed 44.1 kHz; files at 48 kHz or other rates will play slightly fast/slow over Bluetooth (wired output is unaffected). Most music files are already 44.1 kHz.
- **CBR files show exact durations.** VBR files show an estimate that refines itself during the first seconds of playback.
- **Volume is remembered**, as are output mode and your paired device.
- **Album art without the tool:** songs without an `.art` file show a gold record label instead — everything else works normally.

---

## 8. Charging & Power

- Charge via the **USB-C** port (TP4056, ~1A). Charging works while powered off.
- The **slide switch** hard-disconnects the battery — use it for storage or transport.
- Battery life target is ≥4 hours of Bluetooth playback (depends on the cell fitted).

---

## 9. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| **"No SD card!" on boot / empty Library** | Card not FAT32, not inserted fully, or inserted after boot. Power-cycle with the card in. |
| **Board won't boot at all with SD module wired** | If your SD module has a pull-up wired to GPIO 12 — it must not be connected there. See PRD pin rules. |
| **My earbuds never appear in the list** | Make sure they're in *pairing* mode (not just on), and close to the player. Some devices only advertise for ~60s — re-enter pairing mode. |
| **Connected, but no sound** | Check a song is actually playing (Now Playing shows the spinning record) and volume isn't at 0. |
| **Music sounds too fast/slow on Bluetooth** | The file isn't 44.1 kHz. Re-encode it, or use wired output. |
| **Songs play but art shows a plain gold label** | No `.art` file next to the MP3, or an old 240×240 art file. Re-run the pre-scaler tool (`--force` to regenerate). |
| **Playback "frozen" in BT mode** | No sink connected — playback holds until your earbuds connect (or switch to Wired in Settings). |
| **Wrong/garbled colors on screen** | SPI wiring issue on the display (check MOSI/SCK/DC), or a firmware build older than July 2026. |
| **Volume knob scrolls instead of changing volume** | You're on a menu screen — volume control is on Now Playing only. |

Still stuck? Connect USB and open a serial monitor at **115200 baud** — the firmware logs every boot step (`[SD]`, `[BT]`, `[Audio]`, `[UI]`) and most failures are named explicitly there.
