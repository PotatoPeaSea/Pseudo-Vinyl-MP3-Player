"""
Orchestrates the full per-playlist sync:

    Spotify metadata  ->  YouTube match  ->  download MP3  ->  write tags
                      ->  write .art sibling  ->  ready-to-copy folder tree

All cap enforcement lives here (and is mirrored in the GUI) so a run is
auditable: every skip — no YouTube match, over the 15-song cap, download
failure — is reported through on_log, never silent.
"""

import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, List, Optional

from . import naming
from . import spotify_client
from . import youtube_search
from . import youtube_download
from . import tagger
from .limits import MAX_SONGS_PER_PLAYLIST, ART_SIZE

# prescale_art.py lives in the parent tools/prescale_art/ directory. It's a
# top-level module when the app runs from there; add that dir to sys.path as a
# fallback so the pipeline also imports cleanly when driven from elsewhere.
try:
    from prescale_art import resize_and_crop, image_to_rgb565_bytes
except ImportError:  # pragma: no cover - path fallback
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from prescale_art import resize_and_crop, image_to_rgb565_bytes


ART_EXTENSION = ".art"
MP3_EXTENSION = ".mp3"


@dataclass
class PlaylistResult:
    name: str
    folder: str
    synced: int = 0
    skipped_no_match: int = 0
    skipped_error: int = 0
    skipped_over_cap: int = 0


@dataclass
class SyncSummary:
    playlists: List[PlaylistResult] = field(default_factory=list)
    cancelled: bool = False

    @property
    def total_synced(self) -> int:
        return sum(p.synced for p in self.playlists)


# Progress phases reported via on_track_progress(..., phase=...).
PHASE_SEARCH = "searching"
PHASE_DOWNLOAD = "downloading"
PHASE_TAG = "tagging"
PHASE_ART = "art"
PHASE_DONE = "done"


def _noop(*_args, **_kwargs):
    pass


def _write_art_file(art_bytes: bytes, art_path: Path) -> bool:
    """
    Convert cover bytes to a firmware-ready .art (raw RGB565, ART_SIZE square)
    and write it next to the MP3. Returns True on success. Art is optional —
    failure here is logged by the caller, never fatal to the track.
    """
    img = resize_and_crop(art_bytes, size=ART_SIZE)
    rgb565 = image_to_rgb565_bytes(img)
    art_path.write_bytes(rgb565)
    return True


def _sync_one_track(
    track,
    index: int,
    folder: Path,
    on_track_progress,
    on_log,
    is_cancelled,
    result: PlaylistResult,
) -> None:
    label = f"{track.artist} — {track.title}"

    on_track_progress(index, PHASE_SEARCH, track.title)
    match = youtube_search.find_match(track)
    if match is None:
        on_log(f"  ⚠ No acceptable YouTube match for {label} — skipped.")
        result.skipped_no_match += 1
        return

    if is_cancelled():
        return

    mp3_name = naming.track_filename(index, track.title, MP3_EXTENSION)
    mp3_path = folder / mp3_name
    art_path = mp3_path.with_suffix(ART_EXTENSION)

    try:
        on_track_progress(index, PHASE_DOWNLOAD, track.title)
        youtube_download.download_audio(
            match.url,
            mp3_path,
            on_progress=lambda frac: on_track_progress(
                index, PHASE_DOWNLOAD, track.title, frac
            ),
        )
    except youtube_download.FfmpegMissingError:
        raise  # fatal for the whole run — let the caller stop and report
    except youtube_download.DownloadError as e:
        on_log(f"  ⚠ Download failed for {label}: {e} — skipped.")
        result.skipped_error += 1
        return

    if is_cancelled():
        return

    # Fetch art once; reuse the same bytes for both the ID3 APIC tag and the
    # .art sibling (no re-extraction from the finished MP3).
    art_bytes = tagger.fetch_art_bytes(track.art_url)

    on_track_progress(index, PHASE_TAG, track.title)
    try:
        tagger.write_tags(
            mp3_path, track.title, track.artist, track.album, art_bytes
        )
    except Exception as e:
        on_log(f"  ⚠ Tagging failed for {label}: {e} (audio still saved).")

    if art_bytes:
        on_track_progress(index, PHASE_ART, track.title)
        try:
            _write_art_file(art_bytes, art_path)
        except Exception as e:
            on_log(f"  ⚠ Album-art conversion failed for {label}: {e} "
                   f"(song will show the default label on-device).")
    else:
        on_log(f"  ⚠ No album art available for {label} "
               f"(default label on-device).")

    on_track_progress(index, PHASE_DONE, track.title)
    on_log(f"  ✓ {mp3_name}")
    result.synced += 1


def _sync_one_playlist(
    sp,
    playlist: spotify_client.PlaylistSummary,
    playlist_pos: int,
    playlist_total: int,
    output_root: Path,
    on_track_progress,
    on_log,
    is_cancelled,
) -> PlaylistResult:
    folder_name = naming.sanitize_playlist_name(playlist.name, playlist_pos)
    folder = output_root / folder_name
    folder.mkdir(parents=True, exist_ok=True)
    result = PlaylistResult(name=playlist.name, folder=folder_name)

    on_log(f"\n▶ Playlist {playlist_pos}/{playlist_total}: "
           f"{playlist.name!r} → {folder_name}/")

    tracks = spotify_client.get_playlist_tracks(sp, playlist.spotify_id, on_log)

    if len(tracks) > MAX_SONGS_PER_PLAYLIST:
        over = len(tracks) - MAX_SONGS_PER_PLAYLIST
        on_log(f"  ⚠ Playlist has {len(tracks)} tracks; the device caps a "
               f"folder at {MAX_SONGS_PER_PLAYLIST}. Syncing the first "
               f"{MAX_SONGS_PER_PLAYLIST}, skipping {over} (in Spotify order).")
        result.skipped_over_cap = over
        tracks = tracks[:MAX_SONGS_PER_PLAYLIST]

    def track_progress(track_index, phase, title, frac=None):
        on_track_progress(
            playlist_pos, playlist_total,
            track_index, len(tracks),
            title, phase, frac,
        )

    for i, track in enumerate(tracks, start=1):
        if is_cancelled():
            break
        _sync_one_track(
            track, i, folder, track_progress, on_log, is_cancelled, result
        )

    return result


def sync_playlists(
    sp,
    selected_playlists: List[spotify_client.PlaylistSummary],
    output_root,
    on_track_progress: Optional[Callable] = None,
    on_log: Optional[Callable[[str], None]] = None,
    is_cancelled: Optional[Callable[[], bool]] = None,
) -> SyncSummary:
    """
    Sync each selected playlist into output_root/<sanitized name>/.

    Callbacks (all optional):
      on_track_progress(playlist_pos, playlist_total, track_index,
                        track_total, title, phase, frac)
      on_log(message)
      is_cancelled() -> bool   # polled between tracks/playlists

    Caps: the caller (GUI) is expected to have already limited the selection to
    MAX_FOLDER_PLAYLISTS, but the 15-song per-folder cap is enforced here.

    Returns a SyncSummary; raises FfmpegMissingError if ffmpeg is absent
    (nothing can be produced without it).
    """
    on_track_progress = on_track_progress or _noop
    on_log = on_log or _noop
    is_cancelled = is_cancelled or (lambda: False)

    output_root = Path(output_root)
    output_root.mkdir(parents=True, exist_ok=True)

    if not youtube_download.ffmpeg_available():
        raise youtube_download.FfmpegMissingError(
            "ffmpeg was not found on your PATH. Install it and re-run — see "
            "the README's ffmpeg prerequisite section."
        )

    summary = SyncSummary()
    total = len(selected_playlists)
    for pos, playlist in enumerate(selected_playlists, start=1):
        if is_cancelled():
            summary.cancelled = True
            break
        result = _sync_one_playlist(
            sp, playlist, pos, total, output_root,
            on_track_progress, on_log, is_cancelled,
        )
        summary.playlists.append(result)

    if is_cancelled():
        summary.cancelled = True

    return summary
