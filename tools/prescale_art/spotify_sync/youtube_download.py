"""
Download a single YouTube video's audio as an MP3 via yt-dlp + ffmpeg.

ffmpeg is a runtime prerequisite (documented in the README, like Python);
if it's missing we raise a clear, typed error instead of letting yt-dlp fail
deep in a postprocessor with a cryptic message.
"""

import shutil
from pathlib import Path
from typing import Callable, Optional

try:
    import yt_dlp
except ImportError as e:  # pragma: no cover - import guard
    raise ImportError(
        "'yt-dlp' is required for YouTube sourcing. Install with: pip install yt-dlp"
    ) from e


class FfmpegMissingError(RuntimeError):
    """ffmpeg is not on PATH — required to extract MP3 audio."""


class DownloadError(RuntimeError):
    """yt-dlp failed to download or convert the audio."""


def ffmpeg_available() -> bool:
    return shutil.which("ffmpeg") is not None


def download_audio(
    url: str,
    out_path: Path,
    on_progress: Optional[Callable[[float], None]] = None,
    mp3_quality: str = "192",
) -> Path:
    """
    Download the audio track at `url` and write an MP3 to exactly `out_path`
    (extension included). Returns out_path on success.

    on_progress: optional callable(fraction_0_to_1) for UI progress.

    Raises FfmpegMissingError if ffmpeg is absent, DownloadError otherwise.
    """
    if not ffmpeg_available():
        raise FfmpegMissingError(
            "ffmpeg was not found on your PATH. Install it and re-run — see "
            "the README's ffmpeg prerequisite section."
        )

    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # yt-dlp appends the codec extension to `outtmpl`, so give it the stem and
    # let the MP3 postprocessor produce '<stem>.mp3'.
    stem = out_path.with_suffix("")

    def _hook(d):
        if not on_progress:
            return
        if d.get("status") == "downloading":
            total = d.get("total_bytes") or d.get("total_bytes_estimate")
            done = d.get("downloaded_bytes") or 0
            if total:
                on_progress(min(1.0, done / total))
        elif d.get("status") == "finished":
            on_progress(1.0)

    opts = {
        "quiet": True,
        "no_warnings": True,
        "noplaylist": True,
        "format": "bestaudio/best",
        "outtmpl": str(stem) + ".%(ext)s",
        "postprocessors": [
            {
                "key": "FFmpegExtractAudio",
                "preferredcodec": "mp3",
                "preferredquality": mp3_quality,
            }
        ],
        "progress_hooks": [_hook],
    }

    try:
        with yt_dlp.YoutubeDL(opts) as ydl:
            ydl.download([url])
    except yt_dlp.utils.DownloadError as e:
        raise DownloadError(f"yt-dlp failed for {url}: {e}") from e

    produced = stem.with_suffix(".mp3")
    if not produced.exists():
        raise DownloadError(
            f"Download finished but no MP3 was produced for {url} "
            f"(expected {produced.name})."
        )
    if produced != out_path:
        produced.replace(out_path)
    return out_path
