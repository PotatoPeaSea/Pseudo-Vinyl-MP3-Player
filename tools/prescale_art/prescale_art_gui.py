#!/usr/bin/env python3
"""
Pseudo Vinyl MP3 Player — Album Art Converter (GUI)

A drag-and-drop desktop application for preparing music files
for the Pseudo Vinyl MP3 Player. Old-school vinyl aesthetic.
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

# Optional: PIL for background image
try:
    from PIL import Image, ImageTk
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False


# ── Vintage Vinyl Color Palette ──────────────────────────────────────────────

C = {
    # Wood & warm tones
    "bg":            "#2c1a0e",     # Dark walnut
    "bg_mid":        "#3d2415",     # Medium walnut
    "surface":       "#4a2e1a",     # Warm brown panel
    "surface_hover": "#5c3a22",     # Lighter brown hover
    "border":        "#6b4423",     # Rich wood border
    "border_light":  "#8b6340",     # Light wood border

    # Accents — vinyl label gold
    "accent":        "#d4a647",     # Warm gold
    "accent_hover":  "#c4932f",     # Darker gold hover
    "accent_dim":    "#a07830",     # Muted gold

    # Text — parchment & cream
    "text":          "#f0e6d0",     # Cream / parchment
    "text_dim":      "#a08b6e",     # Faded parchment
    "text_dark":     "#2c1a0e",     # Dark text on gold

    # Status colors (muted vintage)
    "success":       "#8fba7a",     # Sage green
    "warning":       "#d4a647",     # Amber
    "error":         "#c76c5a",     # Dusty red

    # Specialty
    "drop_bg":       "#1e120a",     # Very dark wood
    "drop_active":   "#3d2415",     # Highlighted drop
    "progress_bg":   "#1e120a",     # Dark track
    "progress_fill": "#d4a647",     # Gold fill
    "log_bg":        "#1a0f08",     # Darkest wood
    "vinyl_black":   "#1a1a1a",     # Vinyl record black
    "vinyl_groove":  "#2a2a2a",     # Record grooves
}

FONT_FAMILY = "Georgia"
FONT_MONO = "Consolas"


# ── Main Application ─────────────────────────────────────────────────────────

class PseudoVinylConverter:
    def __init__(self):
        # Create root window
        if DND_AVAILABLE:
            self.root = TkinterDnD.Tk()
        else:
            self.root = tk.Tk()

        self.root.title("Pseudo Vinyl — Album Art Converter")
        self.root.geometry("660x800")
        self.root.configure(bg=C["bg"])
        self.root.resizable(True, True)
        self.root.minsize(560, 700)

        # State
        self.source_path = tk.StringVar()
        self.output_path = tk.StringVar()
        self.is_processing = False
        self.cancel_requested = False
        self.stats = {"processed": 0, "skipped": 0, "no_art": 0, "failed": 0, "copied": 0}
        self.bg_image = None
        self.bg_photo = None

        self._build_ui()
        self._center_window()

    # ── UI Construction ──────────────────────────────────────────────────────

    def _build_ui(self):
        # ── Background Canvas (wood texture) ─────────────────────────────────
        self.bg_canvas = tk.Canvas(self.root, highlightthickness=0, bd=0)
        self.bg_canvas.pack(fill="both", expand=True)

        # Load wood background
        self._load_background()
        self.root.bind("<Configure>", self._on_resize)

        # Main frame overlaid on canvas
        main = tk.Frame(self.bg_canvas, bg=C["bg"], padx=24, pady=16)
        self.bg_canvas.create_window(0, 0, window=main, anchor="nw", tags="main")

        # ── Header with vinyl record icon (centered) ────────────────────────
        header = tk.Frame(main, bg=C["bg"])
        header.pack(fill="x", pady=(0, 12))

        header_inner = tk.Frame(header, bg=C["bg"])
        header_inner.pack(anchor="center")

        # Draw a mini vinyl record as the icon
        self.vinyl_canvas = tk.Canvas(
            header_inner, width=56, height=56,
            bg=C["bg"], highlightthickness=0
        )
        self.vinyl_canvas.pack(side="left")
        self._draw_vinyl_icon(self.vinyl_canvas, 28, 28, 26)

        title_frame = tk.Frame(header_inner, bg=C["bg"])
        title_frame.pack(side="left", padx=(10, 0))

        tk.Label(
            title_frame, text="Pseudo Vinyl",
            font=(FONT_FAMILY, 20, "bold italic"),
            bg=C["bg"], fg=C["accent"]
        ).pack(anchor="w")

        tk.Label(
            title_frame, text="Album Art Converter",
            font=(FONT_FAMILY, 11, "italic"),
            bg=C["bg"], fg=C["text_dim"]
        ).pack(anchor="w")

        # Decorative line
        self._draw_ornament(main)

        # ── Drop Zone (vinyl sleeve style) ───────────────────────────────────
        self.drop_frame = tk.Frame(
            main, bg=C["drop_bg"],
            highlightbackground=C["border"], highlightthickness=2,
            cursor="hand2"
        )
        self.drop_frame.pack(fill="x", pady=(8, 16), ipady=24)

        # Inner decorative border
        inner_drop = tk.Frame(self.drop_frame, bg=C["drop_bg"], padx=20, pady=12)
        inner_drop.pack(fill="both", expand=True)

        self.drop_icon_canvas = tk.Canvas(
            inner_drop, width=64, height=64,
            bg=C["drop_bg"], highlightthickness=0
        )
        self.drop_icon_canvas.pack(pady=(8, 4))
        self._draw_record_sleeve(self.drop_icon_canvas, 32, 32)

        self.drop_label = tk.Label(
            inner_drop,
            text="Drop your music folder here" if DND_AVAILABLE else "Click to select your music folder",
            font=(FONT_FAMILY, 13, "bold italic"),
            bg=C["drop_bg"], fg=C["text"]
        )
        self.drop_label.pack()

        self.drop_sublabel = tk.Label(
            inner_drop,
            text="— or click to browse —" if DND_AVAILABLE else "Folders and MP3 files supported",
            font=(FONT_FAMILY, 9, "italic"),
            bg=C["drop_bg"], fg=C["text_dim"]
        )
        self.drop_sublabel.pack(pady=(0, 4))

        # Click to browse on all drop zone widgets
        for widget in [self.drop_frame, inner_drop, self.drop_icon_canvas,
                       self.drop_label, self.drop_sublabel]:
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
        paths_frame.pack(fill="x", pady=(0, 12))

        self._create_path_row(paths_frame, "Source", self.source_path, self._browse_source, 0)
        self._create_path_row(paths_frame, "Output", self.output_path, self._browse_output, 1)

        # ── Convert Button (vinyl label style) ───────────────────────────────
        btn_frame = tk.Frame(main, bg=C["bg"])
        btn_frame.pack(fill="x", pady=(4, 12))

        self.convert_btn = tk.Button(
            btn_frame, text="♫  Press to Cut the Record  ♫",
            font=(FONT_FAMILY, 14, "bold italic"),
            bg=C["accent"], fg=C["text_dark"],
            activebackground=C["accent_hover"], activeforeground=C["text_dark"],
            relief="ridge", cursor="hand2", padx=20, pady=12, bd=3,
            command=self._start_conversion
        )
        self.convert_btn.pack(fill="x")

        # ── Progress ─────────────────────────────────────────────────────────
        progress_outer = tk.Frame(main, bg=C["border"], padx=1, pady=1)
        progress_outer.pack(fill="x", pady=(0, 4))

        self.progress_canvas = tk.Canvas(
            progress_outer, height=10, bg=C["progress_bg"],
            highlightthickness=0, bd=0
        )
        self.progress_canvas.pack(fill="x")

        self.progress_label = tk.Label(
            main, text="", font=(FONT_FAMILY, 9, "italic"),
            bg=C["bg"], fg=C["text_dim"], anchor="w"
        )
        self.progress_label.pack(fill="x", pady=(0, 8))

        # ── Activity Log ─────────────────────────────────────────────────────
        log_header = tk.Frame(main, bg=C["bg"])
        log_header.pack(fill="x")
        tk.Label(
            log_header, text="— Session Log —",
            font=(FONT_FAMILY, 10, "bold italic"),
            bg=C["bg"], fg=C["text_dim"]
        ).pack(side="left")

        log_border = tk.Frame(main, bg=C["border"], bd=2, relief="sunken")
        log_border.pack(fill="both", expand=True, pady=(4, 12))

        self.log_text = tk.Text(
            log_border, bg=C["log_bg"], fg=C["text"],
            font=(FONT_MONO, 9), wrap="word", bd=0,
            padx=12, pady=8, state="disabled", cursor="arrow",
            selectbackground=C["surface_hover"], selectforeground=C["text"],
            insertbackground=C["accent"]
        )
        self.log_text.pack(fill="both", expand=True, side="left")

        scrollbar = tk.Scrollbar(
            log_border, command=self.log_text.yview,
            troughcolor=C["bg_mid"], bg=C["surface"]
        )
        scrollbar.pack(fill="y", side="right")
        self.log_text.configure(yscrollcommand=scrollbar.set)

        # Log text tags
        self.log_text.tag_configure("success", foreground=C["success"])
        self.log_text.tag_configure("warning", foreground=C["warning"])
        self.log_text.tag_configure("error", foreground=C["error"])
        self.log_text.tag_configure("info", foreground=C["accent"])
        self.log_text.tag_configure("dim", foreground=C["text_dim"])

        # ── Stats Bar (vinyl label style) ────────────────────────────────────
        self.stats_frame = tk.Frame(
            main, bg=C["surface"], padx=12, pady=8,
            highlightbackground=C["border"], highlightthickness=1
        )
        self.stats_frame.pack(fill="x", pady=(0, 8))

        self.stats_labels = {}
        stats_items = [
            ("processed", "✦ Pressed", "0"),
            ("copied", "✦ Copied", "0"),
            ("skipped", "✦ Skipped", "0"),
            ("no_art", "✦ No Art", "0"),
            ("failed", "✦ Failed", "0"),
        ]
        for key, label_text, default in stats_items:
            frame = tk.Frame(self.stats_frame, bg=C["surface"])
            frame.pack(side="left", expand=True)
            tk.Label(
                frame, text=label_text,
                font=(FONT_FAMILY, 8, "italic"),
                bg=C["surface"], fg=C["text_dim"]
            ).pack()
            lbl = tk.Label(
                frame, text=default,
                font=(FONT_FAMILY, 14, "bold"),
                bg=C["surface"], fg=C["accent"]
            )
            lbl.pack()
            self.stats_labels[key] = lbl

        # ── Open Folder Button ───────────────────────────────────────────────
        self.open_btn = tk.Button(
            main, text="♪  Open Output Folder  ♪",
            font=(FONT_FAMILY, 10, "italic"),
            bg=C["surface"], fg=C["text"],
            activebackground=C["surface_hover"], activeforeground=C["text"],
            relief="ridge", cursor="hand2", padx=16, pady=6, bd=2,
            command=self._open_output_folder, state="disabled"
        )
        self.open_btn.pack(fill="x")

    # ── Decorative Drawing ───────────────────────────────────────────────────

    def _draw_vinyl_icon(self, canvas, cx, cy, r):
        """Draw a mini vinyl record."""
        # Outer disc
        canvas.create_oval(cx-r, cy-r, cx+r, cy+r, fill=C["vinyl_black"], outline=C["border"])
        # Grooves
        for i in range(3, r-6, 3):
            canvas.create_oval(
                cx-i, cy-i, cx+i, cy+i,
                outline=C["vinyl_groove"], width=1
            )
        # Label (gold center)
        label_r = r // 3
        canvas.create_oval(
            cx-label_r, cy-label_r, cx+label_r, cy+label_r,
            fill=C["accent"], outline=C["accent_dim"]
        )
        # Spindle hole
        canvas.create_oval(cx-2, cy-2, cx+2, cy+2, fill=C["vinyl_black"], outline=C["vinyl_black"])

    def _draw_record_sleeve(self, canvas, cx, cy):
        """Draw a record sleeve / folder icon, centered on cx, cy."""
        s = 24  # half-size of sleeve
        # Sleeve (brown square)
        canvas.create_rectangle(cx-s, cy-s, cx+s, cy+s, fill=C["surface"], outline=C["border"], width=2)
        # Record peeking out
        r = 16
        canvas.create_oval(cx-r, cy-r, cx+r, cy+r, fill=C["vinyl_black"], outline=C["border_light"])
        # Gold label on record
        lr = 4
        canvas.create_oval(cx-lr, cy-lr, cx+lr, cy+lr, fill=C["accent"], outline=C["accent_dim"])
        # Spindle
        canvas.create_oval(cx-1, cy-1, cx+1, cy+1, fill=C["vinyl_black"])

    def _draw_ornament(self, parent):
        """Draw a decorative divider line."""
        ornament = tk.Canvas(parent, height=12, bg=C["bg"], highlightthickness=0)
        ornament.pack(fill="x", pady=(0, 4))
        ornament.bind("<Configure>", lambda e: self._redraw_ornament(ornament))

    def _redraw_ornament(self, canvas):
        canvas.delete("all")
        w = canvas.winfo_width()
        mid = w // 2
        y = 6
        # Left line
        canvas.create_line(20, y, mid - 20, y, fill=C["border_light"], width=1)
        # Center diamond
        canvas.create_polygon(
            mid-6, y, mid, y-4, mid+6, y, mid, y+4,
            fill=C["accent"], outline=C["accent_dim"]
        )
        # Right line
        canvas.create_line(mid + 20, y, w - 20, y, fill=C["border_light"], width=1)

    # ── Background ───────────────────────────────────────────────────────────

    def _load_background(self):
        """Load wood texture background image."""
        if not PIL_AVAILABLE:
            self.bg_canvas.configure(bg=C["bg"])
            return

        # Look for wood_bg.png next to this script
        bg_path = Path(__file__).parent / "wood_bg.png"
        if not bg_path.exists():
            self.bg_canvas.configure(bg=C["bg"])
            return

        try:
            self.bg_image = Image.open(str(bg_path))
            self._update_background()
        except Exception:
            self.bg_canvas.configure(bg=C["bg"])

    def _update_background(self):
        if self.bg_image is None:
            return
        w = self.root.winfo_width() or 660
        h = self.root.winfo_height() or 800
        resized = self.bg_image.resize((w, h), Image.LANCZOS)
        self.bg_photo = ImageTk.PhotoImage(resized)
        self.bg_canvas.delete("bg")
        self.bg_canvas.create_image(0, 0, image=self.bg_photo, anchor="nw", tags="bg")
        self.bg_canvas.tag_lower("bg")

    def _on_resize(self, event):
        if event.widget == self.root:
            self._update_background()
            # Resize main frame to fill
            self.bg_canvas.itemconfigure("main", width=event.width)

    # ── Path Row Helper ──────────────────────────────────────────────────────

    def _create_path_row(self, parent, label, var, browse_cmd, row):
        tk.Label(
            parent, text=f"{label}:", font=(FONT_FAMILY, 10, "bold italic"),
            bg=C["bg"], fg=C["text_dim"], width=7, anchor="w"
        ).grid(row=row, column=0, sticky="w", pady=4)

        entry = tk.Entry(
            parent, textvariable=var, font=(FONT_MONO, 9),
            bg=C["surface"], fg=C["text"], insertbackground=C["accent"],
            relief="sunken", bd=2,
            selectbackground=C["accent_dim"], selectforeground=C["text"]
        )
        entry.grid(row=row, column=1, sticky="ew", padx=(4, 4), pady=4, ipady=3)

        btn = tk.Button(
            parent, text="Browse", font=(FONT_FAMILY, 9),
            bg=C["surface_hover"], fg=C["text"],
            activebackground=C["accent"], activeforeground=C["text_dark"],
            relief="ridge", cursor="hand2", padx=10, bd=2,
            command=browse_cmd
        )
        btn.grid(row=row, column=2, pady=4)

        parent.columnconfigure(1, weight=1)

    # ── Drop Zone Visual Feedback ────────────────────────────────────────────

    def _drop_zone_hover(self, active):
        if self.is_processing:
            return
        bg = C["bg_mid"] if active else C["drop_bg"]
        self.drop_frame.configure(bg=bg)
        for w in self.drop_frame.winfo_children():
            try:
                w.configure(bg=bg)
                for child in w.winfo_children():
                    try:
                        child.configure(bg=bg)
                    except tk.TclError:
                        pass
            except tk.TclError:
                pass

    def _drop_zone_active(self, active):
        border = C["accent"] if active else C["border"]
        self.drop_frame.configure(highlightbackground=border)

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
        parent = str(Path(path).parent)
        folder_name = Path(path).name + "_SD_Ready"
        self.output_path.set(os.path.join(parent, folder_name))

        self.drop_label.configure(text=f"♫ {Path(path).name}")
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
            self.progress_canvas.delete("all")
            w = self.progress_canvas.winfo_width()
            if total > 0:
                fill_w = int(w * current / total)
                # Gold fill with slight gradient effect
                self.progress_canvas.create_rectangle(
                    0, 0, fill_w, 10, fill=C["progress_fill"], outline=""
                )
                # Shine line
                self.progress_canvas.create_line(
                    0, 1, fill_w, 1, fill=C["accent"], width=1
                )
            pct = int(100 * current / total) if total > 0 else 0
            text = f"{current}/{total} ({pct}%)"
            if filename:
                text += f"  ·  {filename}"
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
            text="■  Stop the Press  ■",
            bg=C["error"], fg=C["text"],
            command=self._cancel_conversion
        )
        self.open_btn.configure(state="disabled")
        self.stats = {"processed": 0, "skipped": 0, "no_art": 0, "failed": 0, "copied": 0}
        self._update_stats()

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
        self._log_threadsafe("■ Stopping the press...", "warning")

    def _conversion_worker(self, source, output):
        source_path = Path(source)
        output_path = Path(output)

        self._log_threadsafe(f"Scanning the crates in {source_path.name}/...", "info")

        mp3_files = sorted(source_path.rglob("*.mp3"))
        if not mp3_files:
            self._log_threadsafe("No records found in this crate.", "warning")
            self._finish_conversion()
            return

        self._log_threadsafe(f"Found {len(mp3_files)} tracks ready to press\n", "info")
        total = len(mp3_files)
        start_time = time.time()

        for i, mp3_file in enumerate(mp3_files):
            if self.cancel_requested:
                self._log_threadsafe(f"\n■ Press stopped after {i} tracks.", "warning")
                break

            rel_path = mp3_file.relative_to(source_path)
            dest_mp3 = output_path / rel_path
            dest_art = dest_mp3.with_suffix(".art")
            short_name = str(rel_path)

            self._update_progress(i + 1, total, mp3_file.name)

            try:
                dest_mp3.parent.mkdir(parents=True, exist_ok=True)

                if dest_mp3.exists() and dest_mp3.stat().st_size == mp3_file.stat().st_size:
                    pass
                else:
                    shutil.copy2(str(mp3_file), str(dest_mp3))
                    self.stats["copied"] += 1

                if dest_art.exists() and dest_art.stat().st_mtime >= mp3_file.stat().st_mtime:
                    self.stats["skipped"] += 1
                    continue

                image_data = extract_album_art(mp3_file)
                if image_data is None:
                    self.stats["no_art"] += 1
                    self._log_threadsafe(f"  ♪ {short_name} — no sleeve art", "dim")
                    continue

                img = resize_and_crop(image_data)
                raw_bytes = image_to_rgb565_bytes(img)
                dest_art.write_bytes(raw_bytes)

                self.stats["processed"] += 1
                self._log_threadsafe(f"  ✦ {short_name} → pressed", "success")

            except Exception as e:
                self.stats["failed"] += 1
                self._log_threadsafe(f"  ✗ {short_name} — {e}", "error")

            self._update_stats()

        elapsed = time.time() - start_time
        self._log_threadsafe(f"\n{'─' * 44}", "dim")
        self._log_threadsafe(f"Session complete in {elapsed:.1f}s", "info")
        self._log_threadsafe(
            f"  ✦ {self.stats['processed']} art files pressed  "
            f"♫ {self.stats['copied']} tracks copied  "
            f"→ {self.stats['skipped']} skipped",
            "info"
        )
        if self.stats["no_art"] > 0:
            self._log_threadsafe(
                f"  ♪ {self.stats['no_art']} tracks had no sleeve art",
                "dim"
            )
        self._log_threadsafe(f"\n♫ Output ready at: {output_path}", "info")

        self._finish_conversion()

    def _finish_conversion(self):
        def _restore():
            self.is_processing = False
            self.convert_btn.configure(
                text="♫  Press to Cut the Record  ♫",
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
