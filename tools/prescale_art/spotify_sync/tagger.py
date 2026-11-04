"""
Fetch album art from Spotify and write ID3 tags onto a downloaded MP3.

This is the project's first code that *writes* ID3 text frames — the existing
prescale_art.py only ever reads binary APIC art. We set title/artist/album
(so any generic player shows sane metadata) and embed the cover as APIC (so
the existing extract_album_art path still works if someone re-runs the
art tool on these files).

Note: the firmware itself derives the on-screen title from the *filename*,
not these tags (see docs/spotify-sync-plan.md). Tags are written for
correctness and desktop players, not for the device.
"""

from pathlib import Path
from typing import Optional

try:
    import requests
except ImportError as e:  # pragma: no cover - import guard
    raise ImportError(
        "'requests' is required for album-art fetching. Install with: "
        "pip install requests"
    ) from e

from mutagen.id3 import ID3, ID3NoHeaderError, TIT2, TPE1, TALB, APIC


ART_FETCH_TIMEOUT = 20


def fetch_art_bytes(art_url: Optional[str]) -> Optional[bytes]:
    """
    Download raw cover-image bytes from a Spotify art URL. Returns None if
    there's no URL or the fetch fails (art is optional — never fatal).
    """
    if not art_url:
        return None
    try:
        resp = requests.get(art_url, timeout=ART_FETCH_TIMEOUT)
        resp.raise_for_status()
        return resp.content
    except requests.RequestException:
        return None


def write_tags(
    mp3_path: Path,
    title: str,
    artist: str,
    album: str,
    art_bytes: Optional[bytes] = None,
    art_mime: str = "image/jpeg",
) -> None:
    """
    Write TIT2/TPE1/TALB and (optionally) an APIC cover onto the MP3 at
    mp3_path, replacing any existing frames of those types.
    """
    mp3_path = Path(mp3_path)
    try:
        tags = ID3(str(mp3_path))
    except ID3NoHeaderError:
        tags = ID3()

    tags.setall("TIT2", [TIT2(encoding=3, text=[title])])
    tags.setall("TPE1", [TPE1(encoding=3, text=[artist])])
    tags.setall("TALB", [TALB(encoding=3, text=[album])])

    if art_bytes:
        tags.delall("APIC")
        tags.add(
            APIC(
                encoding=3,
                mime=art_mime,
                type=3,          # front cover
                desc="Cover",
                data=art_bytes,
            )
        )

    tags.save(str(mp3_path), v2_version=3)
