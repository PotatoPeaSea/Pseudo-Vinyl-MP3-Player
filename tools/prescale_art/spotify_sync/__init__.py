"""
Spotify → YouTube → SD-Card sync pipeline for the Pseudo Vinyl MP3 Player.

Uses Spotify only for playlist/track metadata and album art; sources audio
via YouTube search + yt-dlp (Spotify's API cannot serve audio). Assembles
ready-to-copy playlist folders (MP3 + .art sibling) matching the firmware's
SD-card layout.

See docs/spotify-sync-plan.md for the full design.
"""
