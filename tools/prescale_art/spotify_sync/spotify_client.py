"""
Thin, normalized wrappers over an authenticated spotipy.Spotify client.

Everything the pipeline needs from Spotify is reduced to two plain
dataclasses (PlaylistSummary / TrackInfo) so the rest of the code never
touches raw Spotify JSON.
"""

from dataclasses import dataclass
from typing import Callable, List, Optional

# spotipy is imported lazily via the passed-in client, so no direct import
# is needed here.


@dataclass
class PlaylistSummary:
    spotify_id: str
    name: str
    track_count: int
    owner: str


@dataclass
class TrackInfo:
    title: str
    artist: str            # primary (first) artist
    album: str
    duration_ms: int
    art_url: Optional[str]  # largest available cover, or None
    spotify_id: str

    @property
    def duration_s(self) -> float:
        return self.duration_ms / 1000.0


def current_user_name(sp) -> str:
    """Display name of the logged-in user (falls back to the account id)."""
    me = sp.current_user()
    return me.get("display_name") or me.get("id") or "Spotify user"


def list_user_playlists(sp) -> List[PlaylistSummary]:
    """
    All playlists visible to the user (owned, followed, collaborative),
    paginating through the full result set.
    """
    summaries: List[PlaylistSummary] = []
    results = sp.current_user_playlists(limit=50)
    while results:
        for item in results.get("items", []):
            if not item:
                continue
            summaries.append(
                PlaylistSummary(
                    spotify_id=item["id"],
                    name=item.get("name") or "(untitled playlist)",
                    track_count=(item.get("tracks") or {}).get("total", 0),
                    owner=(item.get("owner") or {}).get("display_name")
                    or (item.get("owner") or {}).get("id")
                    or "",
                )
            )
        results = sp.next(results) if results.get("next") else None
    return summaries


def _largest_art_url(album: dict) -> Optional[str]:
    images = (album or {}).get("images") or []
    if not images:
        return None
    # Spotify returns images largest-first, but sort defensively by area.
    best = max(images, key=lambda im: (im.get("width") or 0) * (im.get("height") or 0))
    return best.get("url")


def get_playlist_tracks(
    sp,
    playlist_id: str,
    on_log: Optional[Callable[[str], None]] = None,
) -> List[TrackInfo]:
    """
    Ordered tracks of a playlist, paginating via sp.next(). Local files and
    unavailable/placeholder rows are skipped with a log note rather than
    crashing the fetch.
    """
    def log(msg):
        if on_log:
            on_log(msg)

    tracks: List[TrackInfo] = []
    results = sp.playlist_items(
        playlist_id,
        additional_types=("track",),
        limit=100,
    )
    while results:
        for item in results.get("items", []):
            track = (item or {}).get("track")
            if not track:
                continue
            if track.get("is_local"):
                log(f"  Skipped local file (not available via Spotify): "
                    f"{track.get('name', 'unknown')}")
                continue
            if track.get("type") != "track" or not track.get("id"):
                log(f"  Skipped non-track item: {track.get('name', 'unknown')}")
                continue

            artists = track.get("artists") or []
            primary_artist = artists[0].get("name") if artists else ""
            album = track.get("album") or {}

            tracks.append(
                TrackInfo(
                    title=track.get("name") or "",
                    artist=primary_artist or "",
                    album=album.get("name") or "",
                    duration_ms=track.get("duration_ms") or 0,
                    art_url=_largest_art_url(album),
                    spotify_id=track["id"],
                )
            )
        results = sp.next(results) if results.get("next") else None
    return tracks
