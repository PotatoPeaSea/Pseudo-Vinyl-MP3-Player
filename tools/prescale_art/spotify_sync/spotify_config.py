"""
Spotify app configuration + token-cache location.

PKCE (Proof Key for Code Exchange) flow: no client secret is needed or
shipped, so the Client ID below is safe to bake into the public source.
It identifies *one* Spotify Developer app registered by the project owner;
at personal/family scale a single shared app is fine.

── For fork maintainers ──────────────────────────────────────────────────
If you fork this project and want your own Spotify app:
  1. Create an app at https://developer.spotify.com/dashboard
  2. Under "Redirect URIs", add exactly:  http://127.0.0.1:43813/callback
     (Spotify requires the loopback IP literal 127.0.0.1 — NOT "localhost".)
  3. Swap SPOTIFY_CLIENT_ID below for your app's Client ID.
Nothing else needs to change.
"""

import os
from pathlib import Path

# Public PKCE Client ID — safe to commit (no secret involved). See header.
SPOTIFY_CLIENT_ID = "REPLACE_WITH_YOUR_SPOTIFY_CLIENT_ID"

# Loopback redirect for the PKCE flow. Must be registered verbatim in the
# Spotify app dashboard. Port is fixed (not ephemeral) so the registered URI
# always matches; 43813 is an arbitrary high port unlikely to collide.
REDIRECT_URI = "http://127.0.0.1:43813/callback"
REDIRECT_HOST = "127.0.0.1"
REDIRECT_PORT = 43813
REDIRECT_PATH = "/callback"

# Read-only scopes: list the user's playlists (incl. private/collaborative)
# and read their content. No write scopes — this tool never modifies Spotify.
SPOTIFY_SCOPES = "playlist-read-private playlist-read-collaborative"

# Seconds to wait for the user to complete the browser OAuth before giving up.
AUTH_TIMEOUT_SECONDS = 120

_APP_DIR_NAME = "PseudoVinylConverter"
_TOKEN_CACHE_FILENAME = "spotify_token_cache.json"


def token_cache_path() -> Path:
    """
    Absolute path to the persisted OAuth token cache.

    Prefers %APPDATA%\\PseudoVinylConverter on Windows; falls back to
    ~/.pseudovinyl elsewhere (or if APPDATA is unset). The parent directory
    is created if missing so callers can write straight to the path.
    """
    appdata = os.environ.get("APPDATA")
    if appdata:
        base = Path(appdata) / _APP_DIR_NAME
    else:
        base = Path.home() / ".pseudovinyl"
    base.mkdir(parents=True, exist_ok=True)
    return base / _TOKEN_CACHE_FILENAME


def is_client_id_configured() -> bool:
    """True once SPOTIFY_CLIENT_ID has been set to a real value."""
    return bool(SPOTIFY_CLIENT_ID) and "REPLACE_WITH" not in SPOTIFY_CLIENT_ID
