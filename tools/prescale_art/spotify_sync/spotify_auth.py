"""
Spotify PKCE login driven from a GUI (no console interaction).

spotipy ships a SpotifyPKCE auth manager, but its built-in prompt flow calls
input()/opens a browser and blocks on the console — unusable from a Tk app.
So we use SpotifyPKCE purely as a token/cache manager (it holds the
code_verifier, does the token exchange, and refreshes) while driving the
browser + loopback redirect capture ourselves.

Public API:
    login(on_status=None) -> spotipy.Spotify   # runs the browser flow
    get_client()          -> spotipy.Spotify | None   # from cached token
    is_linked()           -> bool
    unlink()              -> None
"""

import secrets
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

try:
    import spotipy
    from spotipy.oauth2 import SpotifyPKCE
    from spotipy.cache_handler import CacheFileHandler
except ImportError as e:  # pragma: no cover - import guard
    raise ImportError(
        "'spotipy' is required for Spotify sync. Install with: pip install spotipy"
    ) from e

from . import spotify_config as cfg


class SpotifyAuthError(Exception):
    """Raised when the browser OAuth flow fails, is cancelled, or times out."""


# ── Callback capture ──────────────────────────────────────────────────────

_SUCCESS_HTML = b"""<!doctype html><html><head><meta charset="utf-8">
<title>Pseudo Vinyl</title></head>
<body style="font-family:Georgia,serif;background:#2c1a0e;color:#f0e6d0;
text-align:center;padding-top:80px">
<h2 style="color:#d4a647">Spotify linked</h2>
<p>You can close this tab and return to the Pseudo Vinyl Converter.</p>
</body></html>"""

_ERROR_HTML = b"""<!doctype html><html><head><meta charset="utf-8">
<title>Pseudo Vinyl</title></head>
<body style="font-family:Georgia,serif;background:#2c1a0e;color:#f0e6d0;
text-align:center;padding-top:80px">
<h2 style="color:#c76c5a">Link failed</h2>
<p>Return to the Pseudo Vinyl Converter and try again.</p>
</body></html>"""


class _CallbackHandler(BaseHTTPRequestHandler):
    """Single-shot handler that stashes the OAuth query params on the server."""

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path != cfg.REDIRECT_PATH:
            self.send_response(404)
            self.end_headers()
            return

        params = parse_qs(parsed.query)
        # parse_qs gives lists; flatten to first value each.
        self.server.oauth_result = {k: v[0] for k, v in params.items()}

        ok = "code" in self.server.oauth_result
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(_SUCCESS_HTML if ok else _ERROR_HTML)
        self.server.done_event.set()

    def log_message(self, *args):  # silence the default stderr access log
        pass


def _make_auth_manager() -> SpotifyPKCE:
    """SpotifyPKCE configured with our app + persistent file cache."""
    cache_handler = CacheFileHandler(cache_path=str(cfg.token_cache_path()))
    return SpotifyPKCE(
        client_id=cfg.SPOTIFY_CLIENT_ID,
        redirect_uri=cfg.REDIRECT_URI,
        scope=cfg.SPOTIFY_SCOPES,
        cache_handler=cache_handler,
        open_browser=False,   # we open the browser ourselves
    )


# ── Public API ────────────────────────────────────────────────────────────

def is_linked() -> bool:
    """True if a valid (or refreshable) token is already cached."""
    if not cfg.is_client_id_configured():
        return False
    try:
        auth = _make_auth_manager()
        token = auth.cache_handler.get_cached_token()
        if not token:
            return False
        # Refreshes in-place if expired; returns None if refresh fails.
        return auth.validate_token(token) is not None
    except Exception:
        return False


def get_client():
    """
    Return an authenticated spotipy.Spotify from the cached token, or None
    if not linked. The auth manager handles token refresh transparently.
    """
    if not is_linked():
        return None
    return spotipy.Spotify(auth_manager=_make_auth_manager())


def unlink() -> None:
    """Forget the cached token (user must re-link to use Spotify again)."""
    path = cfg.token_cache_path()
    try:
        path.unlink()
    except FileNotFoundError:
        pass


def login(on_status=None):
    """
    Run the interactive PKCE browser flow and return an authenticated
    spotipy.Spotify. Blocks until the user completes login, cancels, or the
    AUTH_TIMEOUT_SECONDS elapses — call this from a background thread.

    on_status: optional callable(str) for progress messages (thread-safe
    marshaling is the caller's responsibility).

    Raises SpotifyAuthError on timeout, state mismatch, denial, or config
    problems.
    """
    if not cfg.is_client_id_configured():
        raise SpotifyAuthError(
            "No Spotify Client ID configured. See spotify_config.py "
            "(SPOTIFY_CLIENT_ID) for setup instructions."
        )

    def _status(msg):
        if on_status:
            on_status(msg)

    auth = _make_auth_manager()
    state = secrets.token_urlsafe(24)
    # get_authorize_url generates + stores the PKCE code_verifier on `auth`;
    # the later get_access_token call reuses that same verifier.
    auth_url = auth.get_authorize_url(state=state)

    try:
        server = HTTPServer((cfg.REDIRECT_HOST, cfg.REDIRECT_PORT), _CallbackHandler)
    except OSError as e:
        raise SpotifyAuthError(
            f"Could not start the local login server on port "
            f"{cfg.REDIRECT_PORT} (is another copy of the app running?): {e}"
        ) from e

    server.oauth_result = None
    server.done_event = threading.Event()

    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()

    try:
        _status("Opening your browser to log in to Spotify…")
        if not webbrowser.open(auth_url):
            _status("Could not open a browser automatically. Copy this URL:")
            _status(auth_url)

        if not server.done_event.wait(timeout=cfg.AUTH_TIMEOUT_SECONDS):
            raise SpotifyAuthError(
                f"Timed out after {cfg.AUTH_TIMEOUT_SECONDS}s waiting for "
                "Spotify login. Please try again."
            )

        result = server.oauth_result or {}
        if "error" in result:
            raise SpotifyAuthError(f"Spotify login was denied: {result['error']}")
        if result.get("state") != state:
            raise SpotifyAuthError(
                "Login response failed a security check (state mismatch). "
                "Please try again."
            )
        code = result.get("code")
        if not code:
            raise SpotifyAuthError("Spotify did not return an authorization code.")

        _status("Exchanging authorization code for a token…")
        # PKCE token exchange using the verifier stored on `auth`; persists
        # to the file cache via the cache handler.
        auth.get_access_token(code)
    finally:
        server.shutdown()
        server.server_close()

    _status("Spotify linked.")
    return spotipy.Spotify(auth_manager=auth)
