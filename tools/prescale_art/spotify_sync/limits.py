"""
Hard library caps, mirrored from firmware/src/config.h.

These are ESP32 RAM constraints, NOT soft preferences — the firmware cannot
show more than this, so the tool must warn and refuse to exceed them rather
than silently produce a card the device will truncate unpredictably.

If the firmware RAM budget ever changes, update BOTH this file and
firmware/src/config.h together.
"""

# firmware/src/config.h:96  — MAX_SONGS. Per-folder song cap; the firmware's
# scanMusic() stops after this many *.mp3 in a folder.
MAX_SONGS_PER_PLAYLIST = 15

# firmware/src/config.h:102 — MAX_PLAYLISTS = 8, BUT scanPlaylists() seeds the
# list with the synthesized "All Songs" root entry before scanning folders,
# so only 8 - 1 = 7 real top-level folders are ever shown on-device. The
# firmware also applies its cap during raw filesystem enumeration (before its
# alphabetical sort), so which folders survive an over-capacity card is
# FAT-order-dependent and non-deterministic. Therefore we enforce 7 upstream.
# Re-check this off-by-one if scanPlaylists() is ever restructured.
MAX_FOLDER_PLAYLISTS = 7

# Album-art side length written into each .art file. The firmware infers the
# side from the file's byte count (side = sqrt(bytes/2)) and validates it is
# <= ART_MAX_SIDE (240, firmware/src/config.h:114). We use the full 240 rather
# than prescale_art.py's DEFAULT_ART_SIZE (90) so art is full display
# resolution; PSRAM on the WROVER absorbs the allocation.
ART_SIZE = 240
