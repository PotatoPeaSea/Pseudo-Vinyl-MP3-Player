#!/usr/bin/env python3
"""
Pseudo Vinyl MP3 Player — Album Art Pre-Scaler

Extracts embedded album art from MP3 files, resizes to 240x240,
and converts to RGB565 raw bitmap (.art) for direct loading on
the ESP32-S3 GC9A01 display.

Usage:
    python prescale_art.py /path/to/music
    python prescale_art.py /path/to/music --format jpeg --quality 85
    python prescale_art.py /path/to/music --force
"""

import argparse
import struct
import sys
import os
import time
from pathlib import Path

try:
    from mutagen.mp3 import MP3
    from mutagen.id3 import ID3, APIC
except ImportError:
    print("ERROR: 'mutagen' is required. Install with: pip install mutagen")
    sys.exit(1)

try:
    from PIL import Image
except ImportError:
    print("ERROR: 'Pillow' is required. Install with: pip install Pillow")
    sys.exit(1)


# ── Constants ────────────────────────────────────────────────────────────────

DISPLAY_SIZE = 240
ART_EXTENSION = ".art"
SUPPORTED_FORMATS = ("rgb565", "jpeg")


# ── RGB565 Conversion ────────────────────────────────────────────────────────

def rgb888_to_rgb565(r: int, g: int, b: int) -> int:
    """Convert an 8-bit RGB pixel to 16-bit RGB565."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def image_to_rgb565_bytes(img: Image.Image) -> bytes:
    """
    Convert a PIL Image to raw RGB565 byte buffer (big-endian).
    Output size: width * height * 2 bytes.
    """
    pixels = img.convert("RGB").load()
    width, height = img.size
    buf = bytearray(width * height * 2)

    idx = 0
    for y in range(height):
        for x in range(width):
            r, g, b = pixels[x, y]
            pixel565 = rgb888_to_rgb565(r, g, b)
            # Big-endian (matches GC9A01 native byte order)
            struct.pack_into(">H", buf, idx, pixel565)
            idx += 2

    return bytes(buf)


# ── Album Art Extraction ─────────────────────────────────────────────────────

def extract_album_art(mp3_path: Path) -> bytes | None:
    """
    Extract the first embedded APIC (album art) image from an MP3 file.
    Returns the raw image bytes, or None if no art is found.
    """
    try:
        tags = ID3(str(mp3_path))
    except Exception:
        return None

    # Look for APIC frames (attached pictures)
    for key in tags:
        if key.startswith("APIC"):
            apic: APIC = tags[key]
            return apic.data

    return None


# ── Image Processing ─────────────────────────────────────────────────────────

def resize_and_crop(image_data: bytes, size: int = DISPLAY_SIZE) -> Image.Image:
    """
    Resize an image to fit within a square, center-cropping if needed.
    Returns a PIL Image of exactly size×size pixels.
    """
    from io import BytesIO

    img = Image.open(BytesIO(image_data))
    img = img.convert("RGB")

    # Center-crop to square
    w, h = img.size
    if w != h:
        side = min(w, h)
        left = (w - side) // 2
        top = (h - side) // 2
        img = img.crop((left, top, left + side, top + side))

    # Resize to target
    img = img.resize((size, size), Image.LANCZOS)
    return img


# ── File Processing ──────────────────────────────────────────────────────────

def should_skip(mp3_path: Path, art_path: Path, force: bool) -> bool:
    """Check if the .art file is already up-to-date."""
    if force:
        return False
    if not art_path.exists():
        return False
    # Skip if art file is newer than the MP3
    return art_path.stat().st_mtime >= mp3_path.stat().st_mtime


def process_file(
    mp3_path: Path,
    output_format: str = "rgb565",
    jpeg_quality: int = 85,
    force: bool = False,
) -> str:
    """
    Process a single MP3 file.
    Returns: "processed", "skipped", "no_art", or "failed"
    """
    if output_format == "rgb565":
        art_path = mp3_path.with_suffix(ART_EXTENSION)
    else:
        art_path = mp3_path.with_suffix(".art.jpg")

    # Check if already processed
    if should_skip(mp3_path, art_path, force):
        return "skipped"

    # Extract album art
    image_data = extract_album_art(mp3_path)
    if image_data is None:
        return "no_art"

    try:
        # Resize and crop
        img = resize_and_crop(image_data)

        # Convert and save
        if output_format == "rgb565":
            raw_bytes = image_to_rgb565_bytes(img)
            art_path.write_bytes(raw_bytes)
        else:
            img.save(str(art_path), "JPEG", quality=jpeg_quality)

        return "processed"

    except Exception as e:
        print(f"  ERROR processing {mp3_path.name}: {e}")
        return "failed"


# ── CLI ──────────────────────────────────────────────────────────────────────

def print_progress_bar(current: int, total: int, width: int = 40):
    """Print an inline progress bar."""
    if total == 0:
        return
    pct = current / total
    filled = int(width * pct)
    bar = "█" * filled + "░" * (width - filled)
    print(f"\r  [{bar}] {current}/{total} ({pct:.0%})", end="", flush=True)


def main():
    parser = argparse.ArgumentParser(
        description="Pseudo Vinyl MP3 Player — Album Art Pre-Scaler",
        epilog="Processes MP3 files and generates 240x240 art files for the ESP32 display.",
    )
    parser.add_argument(
        "directory",
        type=str,
        help="Path to the music directory (scanned recursively)",
    )
    parser.add_argument(
        "--format",
        choices=SUPPORTED_FORMATS,
        default="rgb565",
        help="Output format: rgb565 (raw bitmap, default) or jpeg",
    )
    parser.add_argument(
        "--quality",
        type=int,
        default=85,
        help="JPEG quality (1-100, only used with --format jpeg). Default: 85",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Force re-processing even if .art file is up-to-date",
    )

    args = parser.parse_args()

    music_dir = Path(args.directory).resolve()
    if not music_dir.is_dir():
        print(f"ERROR: '{music_dir}' is not a valid directory.")
        sys.exit(1)

    # Collect MP3 files
    mp3_files = sorted(music_dir.rglob("*.mp3"))
    if not mp3_files:
        print(f"No .mp3 files found in '{music_dir}'.")
        sys.exit(0)

    print(f"╔══════════════════════════════════════════════════╗")
    print(f"║  Pseudo Vinyl — Album Art Pre-Scaler            ║")
    print(f"╚══════════════════════════════════════════════════╝")
    print(f"  Directory : {music_dir}")
    print(f"  MP3 files : {len(mp3_files)}")
    print(f"  Format    : {args.format.upper()}", end="")
    if args.format == "rgb565":
        print(f" ({DISPLAY_SIZE}×{DISPLAY_SIZE} = {DISPLAY_SIZE*DISPLAY_SIZE*2:,} bytes/file)")
    else:
        print(f" (quality={args.quality})")
    print()

    # Process
    stats = {"processed": 0, "skipped": 0, "no_art": 0, "failed": 0}
    start_time = time.time()

    for i, mp3_path in enumerate(mp3_files):
        result = process_file(mp3_path, args.format, args.quality, args.force)
        stats[result] += 1
        print_progress_bar(i + 1, len(mp3_files))

    elapsed = time.time() - start_time
    print()  # newline after progress bar
    print()

    # Summary
    print(f"  ── Summary ({'%.1f' % elapsed}s) ──────────────────────────")
    print(f"  ✅ Processed : {stats['processed']}")
    print(f"  ⏭️  Skipped   : {stats['skipped']} (already up-to-date)")
    print(f"  🎵 No art    : {stats['no_art']} (no embedded album art)")
    if stats["failed"] > 0:
        print(f"  ❌ Failed    : {stats['failed']}")
    print()


if __name__ == "__main__":
    main()
