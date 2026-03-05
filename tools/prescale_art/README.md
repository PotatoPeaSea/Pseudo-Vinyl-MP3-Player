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
