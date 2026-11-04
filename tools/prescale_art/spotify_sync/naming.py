"""
Filesystem-safe folder/file naming for the SD-card output tree.

Sanitization has to satisfy three consumers at once:
  * Windows (folders are created here before being copied to the card),
  * FAT/exFAT on the SD card,
  * the firmware's own scan rules (sd_manager.cpp) — it skips folders that
    start with '.' and synthesizes an "All Songs" entry it must not collide
    with.

Kept as pure functions so the M4 verification can unit-test the edge cases
(reserved names, trailing dots, empty-after-strip, "All Songs" collision).
"""

import re

# Illegal on Windows/FAT filesystems.
_ILLEGAL_CHARS = r'\/:*?"<>|'
_ILLEGAL_RE = re.compile(f"[{re.escape(_ILLEGAL_CHARS)}]")

# Windows reserved device names (case-insensitive, with or without extension).
_RESERVED_NAMES = {
    "CON", "PRN", "AUX", "NUL",
    *(f"COM{i}" for i in range(1, 10)),
    *(f"LPT{i}" for i in range(1, 10)),
}

# The firmware synthesizes this entry for the SD root — a real folder with
# this name would shadow/duplicate it. Reserved.
RESERVED_PLAYLIST_NAME = "All Songs"

_MAX_NAME_LEN = 100  # generous; keeps paths well under FAT limits


def _strip_control_and_illegal(name: str) -> str:
    name = _ILLEGAL_RE.sub(" ", name)
    name = "".join(ch for ch in name if ord(ch) >= 32)  # drop control chars
    name = re.sub(r"\s+", " ", name).strip()
    return name


def sanitize_playlist_name(name: str, fallback_index: int = 1) -> str:
    """
    Turn a Spotify playlist name into a safe top-level folder name.

    Handles: illegal chars, leading '.' (firmware treats it as junk), the
    reserved "All Songs" name, Windows reserved device names, trailing dots/
    spaces (Windows silently strips them), and empty-after-strip (falls back
    to 'Playlist <n>').
    """
    cleaned = _strip_control_and_illegal(name)
    cleaned = cleaned.lstrip(".").strip()          # no leading dot; re-strip
    cleaned = cleaned.rstrip(". ")                 # Windows drops trailing dot/space
    cleaned = cleaned[:_MAX_NAME_LEN].rstrip(". ")

    if not cleaned:
        return f"Playlist {fallback_index}"
    if cleaned.upper() in _RESERVED_NAMES:
        return f"{cleaned} (playlist)"
    if cleaned.lower() == RESERVED_PLAYLIST_NAME.lower():
        return f"{cleaned} (playlist)"
    return cleaned


def sanitize_song_title(title: str) -> str:
    """Safe filename component for a song title (no number prefix here)."""
    cleaned = _strip_control_and_illegal(title)
    cleaned = cleaned.rstrip(". ")[:_MAX_NAME_LEN].rstrip(". ")
    return cleaned or "Untitled"


def track_filename(index: int, title: str, extension: str) -> str:
    """
    'NN - Title.ext' with a zero-padded 1-based track number.

    The numeric prefix preserves playlist order on-device, since the firmware
    sorts by filename-derived title. `extension` includes the dot ('.mp3').
    """
    return f"{index:02d} - {sanitize_song_title(title)}{extension}"
