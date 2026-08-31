#!/usr/bin/env python3
"""
Spotify Sync tab for the Pseudo Vinyl Converter.

Reuses the vintage palette / fonts / threading+logging patterns from
prescale_art_gui.py for visual and behavioural consistency with the existing
Album Art tab. Owns its own link state, playlist list, progress bar and
activity log; drives the sync_pipeline on a background thread.
"""

import os
import sys
import threading
import subprocess
import tkinter as tk
from pathlib import Path

# Shared look-and-feel from the main GUI module.
from prescale_art_gui import C, FONT_FAMILY, FONT_MONO

from spotify_sync import spotify_auth, spotify_client
from spotify_sync import sync_pipeline
from spotify_sync.limits import (
    MAX_SONGS_PER_PLAYLIST,
    MAX_FOLDER_PLAYLISTS,
)
from spotify_sync.youtube_download import FfmpegMissingError


class SpotifySyncTab:
    """Builds and drives the 'Spotify Sync' notebook tab."""

    def __init__(self, parent, root):
        """
        parent : the ttk.Notebook (tab frames are created as its children)
        root   : the Tk root, used for thread-safe .after() marshaling
        """
        self.root = root
        self.frame = tk.Frame(parent, bg=C["bg"], padx=20, pady=14)

        # State
        self.sp = None
        self.playlists = []               # list[PlaylistSummary]
        self.row_vars = []                # list[(BooleanVar, PlaylistSummary)]
        self.output_path = tk.StringVar()
        self.is_busy = False
        self.cancel_requested = False

        self._build_ui()
        self._refresh_link_state()

    # ── UI construction ──────────────────────────────────────────────────

    def _build_ui(self):
        f = self.frame

        tk.Label(
            f, text="Spotify Sync",
            font=(FONT_FAMILY, 15, "bold italic"),
            bg=C["bg"], fg=C["accent"],
        ).pack(anchor="w")
        tk.Label(
            f, text="Link Spotify, pick playlists, and press to assemble "
                    "SD-ready folders.",
            font=(FONT_FAMILY, 9, "italic"),
            bg=C["bg"], fg=C["text_dim"],
        ).pack(anchor="w", pady=(0, 10))

        # ── Link row ─────────────────────────────────────────────────────
        link_row = tk.Frame(f, bg=C["bg"])
        link_row.pack(fill="x", pady=(0, 8))

        self.link_btn = tk.Button(
            link_row, text="Link Spotify Account",
            font=(FONT_FAMILY, 10, "bold"),
            bg=C["accent"], fg=C["text_dark"],
            activebackground=C["accent_hover"], activeforeground=C["text_dark"],
            relief="ridge", cursor="hand2", padx=14, pady=6, bd=2,
            command=self._on_link_clicked,
        )
        self.link_btn.pack(side="left")

        self.link_status = tk.Label(
            link_row, text="Not linked",
            font=(FONT_FAMILY, 10, "italic"),
            bg=C["bg"], fg=C["text_dim"],
        )
        self.link_status.pack(side="left", padx=(12, 0))

        # ── Load playlists ───────────────────────────────────────────────
        self.load_btn = tk.Button(
            f, text="Load My Playlists",
            font=(FONT_FAMILY, 10),
            bg=C["surface_hover"], fg=C["text"],
            activebackground=C["accent"], activeforeground=C["text_dark"],
            relief="ridge", cursor="hand2", padx=10, pady=4, bd=2,
            command=self._on_load_playlists, state="disabled",
        )
        self.load_btn.pack(anchor="w", pady=(0, 8))

        # ── Scrollable playlist checklist ────────────────────────────────
        list_border = tk.Frame(f, bg=C["border"], bd=2, relief="sunken")
        list_border.pack(fill="both", expand=True)

        self.list_canvas = tk.Canvas(
            list_border, bg=C["surface"], highlightthickness=0, height=180
        )
        self.list_canvas.pack(side="left", fill="both", expand=True)
        list_scroll = tk.Scrollbar(
            list_border, command=self.list_canvas.yview,
            troughcolor=C["bg_mid"], bg=C["surface"],
        )
        list_scroll.pack(side="right", fill="y")
        self.list_canvas.configure(yscrollcommand=list_scroll.set)

        self.list_inner = tk.Frame(self.list_canvas, bg=C["surface"])
        self.list_canvas.create_window(
            (0, 0), window=self.list_inner, anchor="nw", tags="inner"
        )
        self.list_inner.bind(
            "<Configure>",
            lambda e: self.list_canvas.configure(
                scrollregion=self.list_canvas.bbox("all")
            ),
        )
        self.list_canvas.bind(
            "<Configure>",
            lambda e: self.list_canvas.itemconfigure("inner", width=e.width),
        )
        self._placeholder = tk.Label(
            self.list_inner,
            text="Link Spotify and load your playlists to choose what to sync.",
            font=(FONT_FAMILY, 9, "italic"),
            bg=C["surface"], fg=C["text_dim"], pady=16,
        )
        self._placeholder.pack()

        # ── Over-cap banner ──────────────────────────────────────────────
        self.banner = tk.Label(
            f, text="", font=(FONT_FAMILY, 9, "bold"),
            bg=C["error"], fg=C["text"], anchor="w",
        )
        # packed/unpacked dynamically by _update_selection_state

        # ── Output row ───────────────────────────────────────────────────
        out_row = tk.Frame(f, bg=C["bg"])
        self.out_row = out_row
        out_row.pack(fill="x", pady=(8, 6))
        tk.Label(
            out_row, text="Output:", font=(FONT_FAMILY, 10, "bold italic"),
            bg=C["bg"], fg=C["text_dim"], width=7, anchor="w",
        ).pack(side="left")
        tk.Entry(
            out_row, textvariable=self.output_path, font=(FONT_MONO, 9),
            bg=C["surface"], fg=C["text"], insertbackground=C["accent"],
            relief="sunken", bd=2,
        ).pack(side="left", fill="x", expand=True, padx=(4, 4), ipady=3)
        tk.Button(
            out_row, text="Browse", font=(FONT_FAMILY, 9),
            bg=C["surface_hover"], fg=C["text"],
            activebackground=C["accent"], activeforeground=C["text_dark"],
            relief="ridge", cursor="hand2", padx=10, bd=2,
            command=self._browse_output,
        ).pack(side="left")

        # ── Start button ─────────────────────────────────────────────────
        self.start_btn = tk.Button(
            f, text="♫  Press to Cut the Record  ♫",
            font=(FONT_FAMILY, 13, "bold italic"),
            bg=C["accent"], fg=C["text_dark"],
            activebackground=C["accent_hover"], activeforeground=C["text_dark"],
            relief="ridge", cursor="hand2", padx=16, pady=10, bd=3,
            command=self._on_start, state="disabled",
        )
        self.start_btn.pack(fill="x", pady=(4, 8))

        # ── Progress ─────────────────────────────────────────────────────
        prog_outer = tk.Frame(f, bg=C["border"], padx=1, pady=1)
        prog_outer.pack(fill="x")
        self.progress_canvas = tk.Canvas(
            prog_outer, height=10, bg=C["progress_bg"], highlightthickness=0, bd=0
        )
        self.progress_canvas.pack(fill="x")
        self.progress_label = tk.Label(
            f, text="", font=(FONT_FAMILY, 9, "italic"),
            bg=C["bg"], fg=C["text_dim"], anchor="w",
        )
        self.progress_label.pack(fill="x", pady=(0, 6))

        # ── Activity log ─────────────────────────────────────────────────
        log_border = tk.Frame(f, bg=C["border"], bd=2, relief="sunken")
        log_border.pack(fill="both", expand=True)
        self.log_text = tk.Text(
            log_border, bg=C["log_bg"], fg=C["text"],
            font=(FONT_MONO, 9), wrap="word", bd=0,
            padx=12, pady=8, state="disabled", cursor="arrow", height=8,
        )
        self.log_text.pack(fill="both", expand=True, side="left")
        log_scroll = tk.Scrollbar(
            log_border, command=self.log_text.yview,
            troughcolor=C["bg_mid"], bg=C["surface"],
        )
        log_scroll.pack(fill="y", side="right")
        self.log_text.configure(yscrollcommand=log_scroll.set)
        self.log_text.tag_configure("success", foreground=C["success"])
        self.log_text.tag_configure("warning", foreground=C["warning"])
        self.log_text.tag_configure("error", foreground=C["error"])
        self.log_text.tag_configure("info", foreground=C["accent"])
        self.log_text.tag_configure("dim", foreground=C["text_dim"])

    # ── Logging / progress (thread-safe) ─────────────────────────────────

    def _log(self, message, tag=None):
        self.log_text.configure(state="normal")
        self.log_text.insert("end", message + "\n", tag or ())
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _log_ts(self, message, tag=None):
        self.root.after(0, self._log, message, tag)

    def _clear_log(self):
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

    def _set_progress(self, fraction, text=""):
        def _update():
            self.progress_canvas.delete("all")
            w = self.progress_canvas.winfo_width()
            fill_w = int(w * max(0.0, min(1.0, fraction)))
            if fill_w > 0:
                self.progress_canvas.create_rectangle(
                    0, 0, fill_w, 10, fill=C["progress_fill"], outline=""
                )
            self.progress_label.configure(text=text)
        self.root.after(0, _update)

    # ── Link flow ────────────────────────────────────────────────────────

    def _refresh_link_state(self):
        if spotify_auth.is_linked():
            self.sp = spotify_auth.get_client()
            try:
                name = spotify_client.current_user_name(self.sp)
            except Exception:
                name = "Spotify"
            self.link_status.configure(
                text=f"Linked as {name}", fg=C["success"]
            )
            self.link_btn.configure(text="Re-link / Switch Account")
            self.load_btn.configure(state="normal")
        else:
            self.link_status.configure(text="Not linked", fg=C["text_dim"])
            self.load_btn.configure(state="disabled")

    def _on_link_clicked(self):
        if self.is_busy:
            return
        self.link_btn.configure(state="disabled")
        self.link_status.configure(text="Opening browser…", fg=C["warning"])
        threading.Thread(target=self._link_worker, daemon=True).start()

    def _link_worker(self):
        try:
            self.sp = spotify_auth.login(
                on_status=lambda m: self.root.after(
                    0, self.link_status.configure, {"text": m, "fg": C["warning"]}
                )
            )
        except Exception as e:
            self.root.after(0, self._link_failed, str(e))
            return
        self.root.after(0, self._link_succeeded)

    def _link_failed(self, msg):
        self.link_status.configure(text=f"Link failed: {msg}", fg=C["error"])
        self.link_btn.configure(state="normal")

    def _link_succeeded(self):
        self.link_btn.configure(state="normal")
        self._refresh_link_state()

    # ── Playlist loading ─────────────────────────────────────────────────

    def _on_load_playlists(self):
        if self.is_busy or not self.sp:
            return
        self.load_btn.configure(state="disabled", text="Loading…")
        threading.Thread(target=self._load_worker, daemon=True).start()

    def _load_worker(self):
        try:
            playlists = spotify_client.list_user_playlists(self.sp)
        except Exception as e:
            self.root.after(0, self._load_failed, str(e))
            return
        self.root.after(0, self._populate_playlists, playlists)

    def _load_failed(self, msg):
        self.load_btn.configure(state="normal", text="Load My Playlists")
        self.link_status.configure(text=f"Could not load playlists: {msg}",
                                   fg=C["error"])

    def _populate_playlists(self, playlists):
        self.playlists = playlists
        self.row_vars = []
        for w in self.list_inner.winfo_children():
            w.destroy()

        if not playlists:
            tk.Label(
                self.list_inner, text="No playlists found on this account.",
                font=(FONT_FAMILY, 9, "italic"),
                bg=C["surface"], fg=C["text_dim"], pady=16,
            ).pack()
        for p in playlists:
            row = tk.Frame(self.list_inner, bg=C["surface"])
            row.pack(fill="x", padx=6, pady=1)

            var = tk.BooleanVar(value=False)
            var.trace_add("write", lambda *_: self._update_selection_state())
            self.row_vars.append((var, p))

            cb = tk.Checkbutton(
                row, variable=var, bg=C["surface"], activebackground=C["surface"],
                selectcolor=C["drop_bg"], highlightthickness=0, bd=0,
            )
            cb.pack(side="left")
            tk.Label(
                row, text=p.name, font=(FONT_FAMILY, 10),
                bg=C["surface"], fg=C["text"], anchor="w",
            ).pack(side="left")

            over = p.track_count > MAX_SONGS_PER_PLAYLIST
            count_text = f"{p.track_count} tracks"
            tk.Label(
                row, text=count_text, font=(FONT_FAMILY, 9, "italic"),
                bg=C["surface"], fg=C["text_dim"],
            ).pack(side="right")
            if over:
                tk.Label(
                    row,
                    text=f"⚠ only first {MAX_SONGS_PER_PLAYLIST} will sync",
                    font=(FONT_FAMILY, 8, "italic"),
                    bg=C["surface"], fg=C["warning"],
                ).pack(side="right", padx=(0, 10))

        self.load_btn.configure(state="normal", text="Load My Playlists")
        self._update_selection_state()

    # ── Selection / cap enforcement ──────────────────────────────────────

    def _selected(self):
        return [p for var, p in self.row_vars if var.get()]

    def _update_selection_state(self):
        selected = self._selected()
        n = len(selected)
        over_folder_cap = n > MAX_FOLDER_PLAYLISTS

        if over_folder_cap:
            self.banner.configure(
                text=f"  ⚠ {n} playlists selected — the device shows at most "
                     f"{MAX_FOLDER_PLAYLISTS} folders. Uncheck "
                     f"{n - MAX_FOLDER_PLAYLISTS} to continue."
            )
            if not self.banner.winfo_ismapped():
                self.banner.pack(fill="x", pady=(6, 0), before=self.out_row)
        else:
            if self.banner.winfo_ismapped():
                self.banner.pack_forget()

        can_start = (
            n > 0
            and not over_folder_cap
            and not self.is_busy
            and bool(self.output_path.get().strip())
        )
        self.start_btn.configure(state="normal" if can_start else "disabled")

    def _browse_output(self):
        from tkinter import filedialog
        path = filedialog.askdirectory(title="Select Output Folder")
        if path:
            self.output_path.set(path)
            self._update_selection_state()

    # ── Sync flow ────────────────────────────────────────────────────────

    def _on_start(self):
        if self.is_busy:
            # acts as cancel while running
            self.cancel_requested = True
            self._log_ts("■ Stopping after the current track…", "warning")
            return

        selected = self._selected()
        output = self.output_path.get().strip()
        if not selected or not output:
            return

        self.is_busy = True
        self.cancel_requested = False
        self._clear_log()
        self._set_progress(0.0, "Starting…")
        self.start_btn.configure(
            text="■  Stop the Press  ■", bg=C["error"], fg=C["text"]
        )
        self.link_btn.configure(state="disabled")
        self.load_btn.configure(state="disabled")

        threading.Thread(
            target=self._sync_worker, args=(selected, output), daemon=True
        ).start()

    def _sync_worker(self, selected, output):
        total = len(selected)

        def on_track_progress(p_pos, p_total, t_idx, t_total, title, phase, frac):
            base = (p_pos - 1) / p_total
            within = (t_idx - 1 + (frac or 0.0)) / max(t_total, 1) / p_total
            label = (f'Playlist {p_pos}/{p_total} · Track {t_idx}/{t_total} '
                     f'· {phase} · "{title}"')
            self._set_progress(base + within, label)

        try:
            summary = sync_pipeline.sync_playlists(
                self.sp, selected, output,
                on_track_progress=on_track_progress,
                on_log=lambda m: self._log_ts(*self._tag_for(m)),
                is_cancelled=lambda: self.cancel_requested,
            )
        except FfmpegMissingError as e:
            self._log_ts(f"\n✗ {e}", "error")
            self.root.after(0, self._finish_sync, None)
            return
        except Exception as e:
            self._log_ts(f"\n✗ Sync failed: {e}", "error")
            self.root.after(0, self._finish_sync, None)
            return

        self.root.after(0, self._finish_sync, summary)

    @staticmethod
    def _tag_for(message):
        """Map a pipeline log line to a colour tag by its leading glyph."""
        if "✓" in message:
            return (message, "success")
        if "⚠" in message:
            return (message, "warning")
        if "✗" in message:
            return (message, "error")
        if message.startswith("▶"):
            return (message, "info")
        return (message, None)

    def _finish_sync(self, summary):
        self.is_busy = False
        self.start_btn.configure(
            text="♫  Press to Cut the Record  ♫",
            bg=C["accent"], fg=C["text_dark"],
        )
        self.link_btn.configure(state="normal")
        self.load_btn.configure(state="normal")

        if summary is not None:
            self._set_progress(1.0, "Done.")
            self._log("\n" + "─" * 44, "dim")
            for pr in summary.playlists:
                self._log(
                    f"  {pr.folder}/  —  {pr.synced} synced, "
                    f"{pr.skipped_no_match} no-match, "
                    f"{pr.skipped_error} failed, "
                    f"{pr.skipped_over_cap} over-cap",
                    "info",
                )
            if summary.cancelled:
                self._log("Sync cancelled.", "warning")
            else:
                self._log(f"Sync complete — {summary.total_synced} tracks ready.",
                          "success")
            out = self.output_path.get().strip()
            if out and os.path.isdir(out):
                self._log(f"♫ Output ready at: {out}", "info")
        self._update_selection_state()
