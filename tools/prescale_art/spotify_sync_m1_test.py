#!/usr/bin/env python3
"""
Throwaway M1 smoke test — run from tools/prescale_art/:

    python spotify_sync_m1_test.py

Requires SPOTIFY_CLIENT_ID to be set in spotify_sync/spotify_config.py first.

Verifies:
  1. Browser login works and returns a client.
  2. Token cache survives — a second run links silently (no browser).
  3. Playlist + track counts come back correctly.

Run it twice: the first run should open a browser, the second should NOT.
"""

import sys

from spotify_sync import spotify_auth, spotify_client, spotify_config


def main():
    if not spotify_config.is_client_id_configured():
        print("ERROR: set SPOTIFY_CLIENT_ID in spotify_sync/spotify_config.py first.")
        return 1

    print(f"Token cache: {spotify_config.token_cache_path()}")

    if spotify_auth.is_linked():
        print("Already linked (cached token) — no browser should open.")
        sp = spotify_auth.get_client()
    else:
        print("Not linked yet — starting browser login…")
        sp = spotify_auth.login(on_status=lambda m: print(f"  [auth] {m}"))

    print(f"\nLogged in as: {spotify_client.current_user_name(sp)}\n")

    playlists = spotify_client.list_user_playlists(sp)
    print(f"Found {len(playlists)} playlist(s):")
    for p in playlists[:20]:
        print(f"  - {p.name!r}  ({p.track_count} tracks, owner={p.owner!r})")
    if len(playlists) > 20:
        print(f"  … and {len(playlists) - 20} more")

    if playlists:
        first = playlists[0]
        print(f"\nFetching tracks for {first.name!r} …")
        tracks = spotify_client.get_playlist_tracks(
            sp, first.spotify_id, on_log=lambda m: print(m)
        )
        print(f"Got {len(tracks)} track(s) "
              f"(playlist reports {first.track_count}):")
        for t in tracks[:10]:
            print(f"  {t.artist} — {t.title}  "
                  f"[{t.duration_s:.0f}s]  art={'yes' if t.art_url else 'no'}")

    print("\nOK — run again to confirm the token cache skips the browser.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
