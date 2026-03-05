#!/usr/bin/env python3
"""
Pseudo Vinyl MP3 Player — Album Art Converter (GUI)

A drag-and-drop desktop application for preparing music files
for the Pseudo Vinyl MP3 Player. Copies MP3s and generates
240x240 RGB565 album art files (.art) ready for the SD card.
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import threading
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from io import BytesIO

# Import core functions from CLI tool
from prescale_art import extract_album_art, resize_and_crop, image_to_rgb565_bytes

# Optional drag-and-drop
try:
    from tkinterdnd2 import TkinterDnD, DND_FILES
    DND_AVAILABLE = True
except (ImportError, RuntimeError, Exception):
    DND_AVAILABLE = False


# ── Color Palette (Catppuccin Mocha-inspired) ────────────────────────────────

C = {
    "bg":            "#1e1e2e",
    "surface":       "#313244",
    "surface_hover": "#45475a",
    "accent":        "#cba6f7",
    "accent_dim":    "#9975c9",
    "text":          "#cdd6f4",
    "text_dim":      "#6c7086",
    "text_dark":     "#1e1e2e",
    "success":       "#a6e3a1",
    "warning":       "#f9e2af",
    "error":         "#f38ba8",
    "border":        "#45475a",
    "drop_bg":       "#252538",
    "drop_active":   "#3b3b55",
    "progress_bg":   "#45475a",
    "progress_fill": "#cba6f7",
    "log_bg":        "#181825",
}

FONT_FAMILY = "Segoe UI"


# ── Main Application ─────────────────────────────────────────────────────────

class PseudoVinylConverter:
    def __init__(self):
        # Create root window
        if DND_AVAILABLE:
            self.root = TkinterDnD.Tk()
        else:
            self.root = tk.Tk()

        self.root.title("Pseudo Vinyl — Album Art Converter")
        self.root.geometry("660x780")
        self.root.configure(bg=C["bg"])
        self.root.resizable(True, True)
        self.root.minsize(560, 680)

        # State
        self.source_path = tk.StringVar()
        self.output_path = tk.StringVar()
        self.is_processing = False
        self.cancel_requested = False
        self.stats = {"processed": 0, "skipped": 0, "no_art": 0, "failed": 0, "copied": 0}

        self._build_ui()
        self._center_window()

    # ── UI Construction ──────────────────────────────────────────────────────

    def _build_ui(self):
        # Main container with padding
        main = tk.Frame(self.root, bg=C["bg"], padx=24, pady=16)
        main.pack(fill="both", expand=True)

        # ── Header ───────────────────────────────────────────────────────────
        header = tk.Frame(main, bg=C["bg"])
        header.pack(fill="x", pady=(0, 16))

        tk.Label(
            header, text="🎵", font=(FONT_FAMILY, 24),
            bg=C["bg"], fg=C["text"]
        ).pack(side="left")

        title_frame = tk.Frame(header, bg=C["bg"])
        title_frame.pack(side="left", padx=(8, 0))

        tk.Label(
            title_frame, text="Pseudo Vinyl",
            font=(FONT_FAMILY, 18, "bold"), bg=C["bg"], fg=C["text"]
        ).pack(anchor="w")

        tk.Label(
            title_frame, text="Album Art Converter",
            font=(FONT_FAMILY, 11), bg=C["bg"], fg=C["text_dim"]
        ).pack(anchor="w")

        # ── Drop Zone ────────────────────────────────────────────────────────
        self.drop_frame = tk.Frame(
            main, bg=C["drop_bg"], highlightbackground=C["border"],
            highlightthickness=2, cursor="hand2"
        )
        self.drop_frame.pack(fill="x", pady=(0, 16), ipady=28)

        self.drop_icon = tk.Label(
            self.drop_frame, text="📂", font=(FONT_FAMILY, 32),
            bg=C["drop_bg"], fg=C["text"]
        )
        self.drop_icon.pack(pady=(16, 4))

        self.drop_label = tk.Label(
            self.drop_frame,
            text="Drop your music folder here" if DND_AVAILABLE else "Click to select your music folder",
            font=(FONT_FAMILY, 12, "bold"), bg=C["drop_bg"], fg=C["text"]
        )
        self.drop_label.pack()

        self.drop_sublabel = tk.Label(
            self.drop_frame,
            text="— or click to browse —" if DND_AVAILABLE else "Folders and MP3 files supported",
            font=(FONT_FAMILY, 9), bg=C["drop_bg"], fg=C["text_dim"]
        )
        self.drop_sublabel.pack(pady=(0, 8))

        # Click to browse
        for widget in [self.drop_frame, self.drop_icon, self.drop_label, self.drop_sublabel]:
            widget.bind("<Button-1>", lambda e: self._browse_source())
            widget.bind("<Enter>", lambda e: self._drop_zone_hover(True))
            widget.bind("<Leave>", lambda e: self._drop_zone_hover(False))

        # Drag-and-drop registration
        if DND_AVAILABLE:
            self.drop_frame.drop_target_register(DND_FILES)
            self.drop_frame.dnd_bind("<<DropEnter>>", lambda e: self._drop_zone_active(True))
            self.drop_frame.dnd_bind("<<DropLeave>>", lambda e: self._drop_zone_active(False))
            self.drop_frame.dnd_bind("<<Drop>>", self._on_drop)

        # ── Path Fields ──────────────────────────────────────────────────────
        paths_frame = tk.Frame(main, bg=C["bg"])
        paths_frame.pack(fill="x", pady=(0, 16))

        # Source path
        self._create_path_row(paths_frame, "Source", self.source_path, self._browse_source, row=0)
        # Output path
        self._create_path_row(paths_frame, "Output", self.output_path, self._browse_output, row=1)

        # ── Convert Button ───────────────────────────────────────────────────
        btn_frame = tk.Frame(main, bg=C["bg"])
        btn_frame.pack(fill="x", pady=(0, 12))

        self.convert_btn = tk.Button(
            btn_frame, text="🎵  Convert & Prepare SD Card",
            font=(FONT_FAMILY, 13, "bold"),
            bg=C["accent"], fg=C["text_dark"],
            activebackground=C["accent_dim"], activeforeground=C["text_dark"],
            relief="flat", cursor="hand2", padx=20, pady=10,
            command=self._start_conversion
        )
        self.convert_btn.pack(fill="x")

        # ── Progress ─────────────────────────────────────────────────────────
        progress_frame = tk.Frame(main, bg=C["bg"])
        progress_frame.pack(fill="x", pady=(0, 4))

        self.progress_canvas = tk.Canvas(
            progress_frame, height=8, bg=C["progress_bg"],
            highlightthickness=0, bd=0
        )
        self.progress_canvas.pack(fill="x")

        self.progress_label = tk.Label(
            main, text="", font=(FONT_FAMILY, 9), bg=C["bg"], fg=C["text_dim"],
            anchor="w"
        )
        self.progress_label.pack(fill="x", pady=(0, 8))

        # ── Log Area ─────────────────────────────────────────────────────────
        log_header = tk.Frame(main, bg=C["bg"])
        log_header.pack(fill="x")
        tk.Label(
            log_header, text="Activity Log", font=(FONT_FAMILY, 10, "bold"),
            bg=C["bg"], fg=C["text_dim"]
        ).pack(side="left")

        log_container = tk.Frame(main, bg=C["border"], bd=1)
        log_container.pack(fill="both", expand=True, pady=(4, 12))

        self.log_text = tk.Text(
            log_container, bg=C["log_bg"], fg=C["text"],
            font=("Consolas", 9), wrap="word", bd=0,
            padx=10, pady=8, state="disabled", cursor="arrow",
            selectbackground=C["surface_hover"], selectforeground=C["text"]
        )
        self.log_text.pack(fill="both", expand=True, side="left")

        scrollbar = tk.Scrollbar(log_container, command=self.log_text.yview)
        scrollbar.pack(fill="y", side="right")
        self.log_text.configure(yscrollcommand=scrollbar.set)

        # Log text tags for colors
        self.log_text.tag_configure("success", foreground=C["success"])
        self.log_text.tag_configure("warning", foreground=C["warning"])
        self.log_text.tag_configure("error", foreground=C["error"])
        self.log_text.tag_configure("info", foreground=C["accent"])
        self.log_text.tag_configure("dim", foreground=C["text_dim"])

        # ── Stats Bar ────────────────────────────────────────────────────────
        self.stats_frame = tk.Frame(main, bg=C["surface"], padx=12, pady=8)
        self.stats_frame.pack(fill="x", pady=(0, 8))

        self.stats_labels = {}
        stats_items = [
            ("processed", "✅ Processed", "0"),
            ("copied", "📋 Copied", "0"),
            ("skipped", "⏭️ Skipped", "0"),
            ("no_art", "🎵 No Art", "0"),
            ("failed", "❌ Failed", "0"),
        ]
        for key, label_text, default in stats_items:
            frame = tk.Frame(self.stats_frame, bg=C["surface"])
            frame.pack(side="left", expand=True)
            tk.Label(
                frame, text=label_text, font=(FONT_FAMILY, 8),
                bg=C["surface"], fg=C["text_dim"]
            ).pack()
            lbl = tk.Label(
                frame, text=default, font=(FONT_FAMILY, 14, "bold"),
                bg=C["surface"], fg=C["text"]
            )
            lbl.pack()
            self.stats_labels[key] = lbl

        # ── Open Folder Button ───────────────────────────────────────────────
        self.open_btn = tk.Button(
            main, text="📁 Open Output Folder",
            font=(FONT_FAMILY, 10),
            bg=C["surface"], fg=C["text"],
            activebackground=C["surface_hover"], activeforeground=C["text"],
            relief="flat", cursor="hand2", padx=16, pady=6,
            command=self._open_output_folder, state="disabled"
        )
        self.open_btn.pack(fill="x")

    def _create_path_row(self, parent, label, var, browse_cmd, row):
        tk.Label(
            parent, text=f"{label}:", font=(FONT_FAMILY, 10, "bold"),
            bg=C["bg"], fg=C["text_dim"], width=7, anchor="w"
        ).grid(row=row, column=0, sticky="w", pady=4)

        entry = tk.Entry(
            parent, textvariable=var, font=(FONT_FAMILY, 9),
            bg=C["surface"], fg=C["text"], insertbackground=C["text"],
            relief="flat", bd=0
        )
        entry.grid(row=row, column=1, sticky="ew", padx=(4, 4), pady=4, ipady=4)

        btn = tk.Button(
            parent, text="Browse", font=(FONT_FAMILY, 9),
            bg=C["surface_hover"], fg=C["text"],
            activebackground=C["accent_dim"], activeforeground=C["text_dark"],
            relief="flat", cursor="hand2", padx=10,
            command=browse_cmd
        )
        btn.grid(row=row, column=2, pady=4)

        parent.columnconfigure(1, weight=1)

    # ── Drop Zone Visual Feedback ────────────────────────────────────────────

    def _drop_zone_hover(self, active):
        if self.is_processing:
            return
        bg = C["surface"] if active else C["drop_bg"]
        for w in [self.drop_frame, self.drop_icon, self.drop_label, self.drop_sublabel]:
            w.configure(bg=bg)

    def _drop_zone_active(self, active):
        bg = C["drop_active"] if active else C["drop_bg"]
        border = C["accent"] if active else C["border"]
        self.drop_frame.configure(bg=bg, highlightbackground=border)
        for w in [self.drop_icon, self.drop_label, self.drop_sublabel]:
            w.configure(bg=bg)

    # ── File/Folder Selection ────────────────────────────────────────────────

    def _browse_source(self):
        if self.is_processing:
            return
        path = filedialog.askdirectory(title="Select Music Folder")
        if path:
            self._set_source(path)

    def _browse_output(self):
        if self.is_processing:
            return
        path = filedialog.askdirectory(title="Select Output Folder")
        if path:
            self.output_path.set(path)

    def _on_drop(self, event):
        if self.is_processing:
            return
        # Parse dropped paths (may contain braces for paths with spaces)
        data = event.data
        if data.startswith("{"):
            paths = [p.strip("{}") for p in data.split("} {")]
        else:
            paths = data.split()

        if paths:
            path = paths[0]
            if os.path.isfile(path):
                path = os.path.dirname(path)
            self._set_source(path)

        self._drop_zone_active(False)

    def _set_source(self, path):
        self.source_path.set(path)
        # Auto-set output path
        parent = str(Path(path).parent)
        folder_name = Path(path).name + "_SD_Ready"
        self.output_path.set(os.path.join(parent, folder_name))

        # Update drop zone
        self.drop_label.configure(text=f"📂 {Path(path).name}")
        self.drop_sublabel.configure(text="Click or drop again to change")
        self._log(f"Selected: {path}", "info")

    # ── Logging ──────────────────────────────────────────────────────────────

    def _log(self, message, tag=None):
        self.log_text.configure(state="normal")
        if tag:
            self.log_text.insert("end", message + "\n", tag)
        else:
            self.log_text.insert("end", message + "\n")
        self.log_text.see("end")
        self.log_text.configure(state="disabled")

    def _log_threadsafe(self, message, tag=None):
        self.root.after(0, self._log, message, tag)

    # ── Progress ─────────────────────────────────────────────────────────────

    def _update_progress(self, current, total, filename=""):
        def _update():
            # Progress bar
            self.progress_canvas.delete("all")
            w = self.progress_canvas.winfo_width()
            if total > 0:
                fill_w = int(w * current / total)
                self.progress_canvas.create_rectangle(
                    0, 0, fill_w, 8, fill=C["progress_fill"], outline=""
                )
            # Label
            pct = int(100 * current / total) if total > 0 else 0
            text = f"{current}/{total} ({pct}%)"
            if filename:
                text += f"  •  {filename}"
            self.progress_label.configure(text=text)
        self.root.after(0, _update)

    def _update_stats(self):
        def _update():
            for key, lbl in self.stats_labels.items():
                lbl.configure(text=str(self.stats.get(key, 0)))
        self.root.after(0, _update)

    # ── Conversion ───────────────────────────────────────────────────────────

    def _start_conversion(self):
        source = self.source_path.get().strip()
        output = self.output_path.get().strip()

        if not source or not os.path.isdir(source):
            messagebox.showerror("Error", "Please select a valid source music folder.")
            return
        if not output:
            messagebox.showerror("Error", "Please select an output folder.")
            return

        self.is_processing = True
        self.cancel_requested = False
        self.convert_btn.configure(
            text="⏹  Cancel", bg=C["error"], fg=C["text"],
            command=self._cancel_conversion
        )
        self.open_btn.configure(state="disabled")
        self.stats = {"processed": 0, "skipped": 0, "no_art": 0, "failed": 0, "copied": 0}
        self._update_stats()

        # Clear log
        self.log_text.configure(state="normal")
        self.log_text.delete("1.0", "end")
        self.log_text.configure(state="disabled")

        thread = threading.Thread(
            target=self._conversion_worker,
            args=(source, output),
            daemon=True
        )
        thread.start()

    def _cancel_conversion(self):
        self.cancel_requested = True
        self._log_threadsafe("⏹ Cancellation requested...", "warning")

    def _conversion_worker(self, source, output):
        source_path = Path(source)
        output_path = Path(output)

        self._log_threadsafe(f"Scanning {source_path.name}/ for MP3 files...", "info")

        # Find all MP3 files
        mp3_files = sorted(source_path.rglob("*.mp3"))
        if not mp3_files:
            self._log_threadsafe("No MP3 files found in the selected folder.", "warning")
            self._finish_conversion()
            return

        self._log_threadsafe(f"Found {len(mp3_files)} MP3 files\n", "info")
        total = len(mp3_files)
        start_time = time.time()

        for i, mp3_file in enumerate(mp3_files):
            if self.cancel_requested:
                self._log_threadsafe(f"\n⏹ Cancelled after {i} files.", "warning")
                break

            # Calculate relative path for folder structure
            rel_path = mp3_file.relative_to(source_path)
            dest_mp3 = output_path / rel_path
            dest_art = dest_mp3.with_suffix(".art")
            short_name = str(rel_path)

            self._update_progress(i + 1, total, mp3_file.name)

            try:
                # Create output directory
                dest_mp3.parent.mkdir(parents=True, exist_ok=True)

                # Copy MP3 file (skip if already exists and same size)
                if dest_mp3.exists() and dest_mp3.stat().st_size == mp3_file.stat().st_size:
                    pass  # Already copied
                else:
                    shutil.copy2(str(mp3_file), str(dest_mp3))
                    self.stats["copied"] += 1

                # Check if art already exists and is up-to-date
                if dest_art.exists() and dest_art.stat().st_mtime >= mp3_file.stat().st_mtime:
                    self.stats["skipped"] += 1
                    continue

                # Extract album art
                image_data = extract_album_art(mp3_file)
                if image_data is None:
                    self.stats["no_art"] += 1
                    self._log_threadsafe(f"  🎵 {short_name} — no album art", "dim")
                    continue

                # Resize, convert, and save
                img = resize_and_crop(image_data)
                raw_bytes = image_to_rgb565_bytes(img)
                dest_art.write_bytes(raw_bytes)

                self.stats["processed"] += 1
                self._log_threadsafe(f"  ✅ {short_name} → .art", "success")

            except Exception as e:
                self.stats["failed"] += 1
                self._log_threadsafe(f"  ❌ {short_name} — {e}", "error")

            self._update_stats()

        elapsed = time.time() - start_time
        self._log_threadsafe(f"\n{'─' * 40}", "dim")
        self._log_threadsafe(f"Done in {elapsed:.1f}s", "info")
        self._log_threadsafe(
            f"  ✅ {self.stats['processed']} art files created  "
            f"📋 {self.stats['copied']} MP3s copied  "
            f"⏭️ {self.stats['skipped']} skipped",
            "info"
        )
        if self.stats["no_art"] > 0:
            self._log_threadsafe(
                f"  🎵 {self.stats['no_art']} files had no embedded album art",
                "dim"
            )
        self._log_threadsafe(f"\n📁 Output ready at: {output_path}", "info")

        self._finish_conversion()

    def _finish_conversion(self):
        def _restore():
            self.is_processing = False
            self.convert_btn.configure(
                text="🎵  Convert & Prepare SD Card",
                bg=C["accent"], fg=C["text_dark"],
                command=self._start_conversion
            )
            self.open_btn.configure(state="normal")
        self.root.after(0, _restore)

    # ── Actions ──────────────────────────────────────────────────────────────

    def _open_output_folder(self):
        output = self.output_path.get().strip()
        if output and os.path.isdir(output):
            if sys.platform == "win32":
                os.startfile(output)
            elif sys.platform == "darwin":
                subprocess.run(["open", output])
            else:
                subprocess.run(["xdg-open", output])

    # ── Helpers ──────────────────────────────────────────────────────────────

    def _center_window(self):
        self.root.update_idletasks()
        w = self.root.winfo_width()
        h = self.root.winfo_height()
        x = (self.root.winfo_screenwidth() - w) // 2
        y = (self.root.winfo_screenheight() - h) // 2
        self.root.geometry(f"+{x}+{y}")

    def run(self):
        self.root.mainloop()


# ── Entry Point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    app = PseudoVinylConverter()
    app.run()
