"""
Find the best YouTube audio match for a Spotify track.

Pure, side-effect-free helpers (no download): build a query, pull a handful
of metadata-only candidates via yt-dlp, score them against the Spotify
track, and pick the best — or decide there is no acceptable match.

Matching philosophy: duration is the strongest signal (a correct match is
almost always within a few seconds), so it's used as a hard gate first, then
combined with fuzzy title/uploader similarity. "Live/remix/cover"-type terms
are penalized, not banned, and the penalty is waived when the Spotify track
itself is that kind of thing (e.g. an actual live album).
"""

import difflib
import re
from dataclasses import dataclass
from typing import List, Optional

try:
    import yt_dlp
except ImportError as e:  # pragma: no cover - import guard
    raise ImportError(
        "'yt-dlp' is required for YouTube sourcing. Install with: pip install yt-dlp"
    ) from e


# Terms that usually indicate the wrong version of a song. Penalized unless
# the Spotify track/album name contains the same term (then it's expected).
BLACKLIST_TERMS = (
    "live", "cover", "remix", "8d audio", "sped up", "slowed",
    "nightcore", "karaoke", "instrumental", "reverb", "loop",
)

# Duration gate: a candidate is rejected outright if it differs from the
# Spotify duration by more than max(HARD_SECONDS, HARD_FRACTION * duration).
DURATION_HARD_SECONDS = 12.0
DURATION_HARD_FRACTION = 0.08

# Below this final score, we treat it as "no acceptable match".
SCORE_FLOOR = 0.45

# Number of YouTube results to consider per query.
NUM_CANDIDATES = 5


@dataclass
class Candidate:
    video_id: str
    title: str
    uploader: str
    duration_s: float

    @property
    def url(self) -> str:
        return f"https://www.youtube.com/watch?v={self.video_id}"


def _normalize(text: str) -> str:
    """Lowercase, strip bracketed noise and punctuation for fuzzy matching."""
    text = text.lower()
    text = re.sub(r"[\(\[\{].*?[\)\]\}]", " ", text)  # drop (...) [...] {...}
    text = re.sub(r"[^a-z0-9]+", " ", text)
    return text.strip()


def build_search_query(track) -> str:
    """
    A plain 'Artist Title' query. Deliberately omits words like 'official'
    or 'audio' — adding them tends to bias toward music videos (often longer,
    with intros) over plain audio uploads.
    """
    return f"{track.artist} {track.title}".strip()


def search_candidates(query: str, n: int = NUM_CANDIDATES) -> List[Candidate]:
    """
    Metadata-only YouTube search (no download). Returns up to n Candidates.

    Uses extract_flat so yt-dlp doesn't resolve full stream info for every
    hit — fast, and enough for duration/title scoring.
    """
    opts = {
        "quiet": True,
        "no_warnings": True,
        "extract_flat": "in_playlist",
        "skip_download": True,
        "default_search": "ytsearch",
    }
    candidates: List[Candidate] = []
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(f"ytsearch{n}:{query}", download=False)

    for entry in (info or {}).get("entries", []) or []:
        if not entry:
            continue
        vid = entry.get("id")
        if not vid:
            continue
        dur = entry.get("duration")
        candidates.append(
            Candidate(
                video_id=vid,
                title=entry.get("title") or "",
                uploader=entry.get("uploader") or entry.get("channel") or "",
                duration_s=float(dur) if dur else 0.0,
            )
        )
    return candidates


def _duration_within_gate(track, cand: Candidate) -> bool:
    if cand.duration_s <= 0:
        # Unknown duration (flat search sometimes omits it) — don't hard-reject;
        # let scoring handle it with no duration bonus.
        return True
    target = track.duration_s
    allowed = max(DURATION_HARD_SECONDS, DURATION_HARD_FRACTION * target)
    return abs(cand.duration_s - target) <= allowed


def _blacklist_penalty(track, cand: Candidate) -> float:
    cand_text = _normalize(f"{cand.title} {cand.uploader}")
    spotify_text = _normalize(f"{track.title} {track.album}")
    penalty = 0.0
    for term in BLACKLIST_TERMS:
        nterm = _normalize(term)
        if nterm and nterm in cand_text and nterm not in spotify_text:
            penalty += 0.15
    return penalty


def score_candidate(track, cand: Candidate) -> float:
    """
    Combined similarity score in roughly [0, 1]. Higher is better.

    Blend: title similarity (dominant), a small artist/uploader bonus, a
    duration-closeness bonus, minus blacklist penalties.
    """
    spotify_title = _normalize(track.title)
    cand_title = _normalize(cand.title)

    title_sim = difflib.SequenceMatcher(None, spotify_title, cand_title).ratio()

    # The candidate title usually contains the artist too; reward that.
    artist_norm = _normalize(track.artist)
    artist_bonus = 0.1 if artist_norm and artist_norm in cand_title else 0.0
    # Or the uploader is the artist / an official channel.
    uploader_norm = _normalize(cand.uploader)
    if artist_norm and artist_norm in uploader_norm:
        artist_bonus = max(artist_bonus, 0.1)

    if cand.duration_s > 0 and track.duration_s > 0:
        diff = abs(cand.duration_s - track.duration_s)
        # 1.0 at exact match, decaying to 0 at a 30s gap.
        duration_bonus = max(0.0, 1.0 - diff / 30.0) * 0.2
    else:
        duration_bonus = 0.0

    penalty = _blacklist_penalty(track, cand)

    score = title_sim + artist_bonus + duration_bonus - penalty
    return max(0.0, min(1.0, score))


def pick_best_match(track, candidates: List[Candidate]) -> Optional[Candidate]:
    """
    Best candidate passing the duration gate and the score floor, or None
    if nothing is acceptable (caller should log + skip, not crash).
    """
    gated = [c for c in candidates if _duration_within_gate(track, c)]
    if not gated:
        return None
    best = max(gated, key=lambda c: score_candidate(track, c))
    if score_candidate(track, best) < SCORE_FLOOR:
        return None
    return best


def find_match(track) -> Optional[Candidate]:
    """Convenience: build query, search, and pick — the whole search step."""
    query = build_search_query(track)
    candidates = search_candidates(query)
    return pick_best_match(track, candidates)
