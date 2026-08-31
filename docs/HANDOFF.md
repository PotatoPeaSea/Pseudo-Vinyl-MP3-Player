# Handoff — spin/shadow follow-ups + an open duration-display bug

Items 1 and 2 were implemented and flashed 2026-07-23 (same COM10 board as
the WROVER bring-up). Item 3 is a new bug found by the user after flashing
item 1's fix and is **not yet root-caused** — that's the next thing to pick
up.

---

## 1. True 60fps vinyl spin

**Status:** implemented, flashed, AND visually confirmed by the user on the
panel — smoother than before.

Two changes were needed, in this order:

1. `firmware/src/lv_conf.h`:
   ```c
   #define LV_DISP_DEF_REFR_PERIOD  16   // ms — match UI_REFRESH_MS for a real 60fps redraw
   ```
   This was previously unset, silently falling back to LVGL's default 30ms
   (~33fps) — `vinyl_angle` advanced at 60Hz but only ~33 of those steps/sec
   ever reached the screen.
2. Even with a genuine 60Hz redraw, the user reported it still looked
   jittery — the redraw rate was never the whole problem. `VINYL_SPIN_SPEED_DEG`
   in `firmware/src/config.h` was **2°/frame** (120°/s, one rotation every
   3s); each per-frame jump was large enough to look stepped/strobed even
   at a confirmed 60Hz redraw. Halved to **1°/frame** (60°/s, one rotation
   every 6s) — user confirmed this looks better. If it's still not smooth
   enough, keep lowering this value (tradeoff: slower apparent rotation)
   rather than looking for another redraw-rate problem — the redraw rate
   itself is now confirmed fine.

Zero RAM cost either way (both are timing/step constants, not buffers).
Builds clean on both `esp32dev` and `esp32dev-debug`.

**Still open:** if a USB power meter is available, comparing idle-playback
current at 30ms vs. 16ms `LV_DISP_DEF_REFR_PERIOD` would answer the "is the
power cost noticeable" question raised in the original analysis — still
unmeasured.

---

## 2. Drop shadow on the Now Playing title text

**Status:** implemented (stacked dual-label technique), flashed. Visual
legibility and — critically — the scrolling title's shadow staying in sync
over a long scroll are NOT yet verified on hardware.

Implementation, in `firmware/src/display/ui_manager.cpp`:

- `COL_TEXT_SHADOW` (near-black) added to the color block.
- `np_title_label_shadow` and `np_time_label_shadow` — duplicate labels,
  same text/font/width/alignment as the real ones, offset `+1,+1`,
  `COL_TEXT_SHADOW` at `LV_OPA_60`, created *before* (so behind, in z-order)
  their real counterparts.
- `UI::setNowPlaying` / `UI::setProgress` write both the real and shadow
  label in the same call, guarded by the existing change caches so this
  doesn't add extra 60Hz label churn.
- `clearWidgetRefs()` nulls both shadow pointers on screen teardown.

**Known risk, called out in the original analysis and still unverified:**
`np_title_label` scrolls (`LV_LABEL_LONG_SCROLL_CIRCULAR`); its shadow copy
was built with identical properties so LVGL's scroll animation should track
in lockstep, but this needs eyes on the device over *several full scroll
loops*, not a glance — small creation-order/timing differences are the
likely failure mode if it drifts. `np_time_label` doesn't scroll, so its
shadow is much lower-risk and was done first per the original recommendation
to prove the technique.

**Fallback, if the title shadow is seen to drift on hardware:** replace the
dual-label pair for `np_title_label` with a low-opacity dark backing plate
(a plain `lv_obj` sized to the label's bounding box, `bg_opa`+`bg_color`,
same pattern `np_art_holder` already uses for circular clipping) — simpler
to keep in sync, though not a true per-glyph shadow. Not implemented; only
worth doing if the dual-label title is confirmed broken on hardware.

---

## 3. Now Playing total-duration is wrong — four fix attempts, latest flashed NOT yet confirmed

**Status:** the "keeps changing" symptom (attempts 1–3) and the "wrong
value" symptom (attempt 4) turned out to be two separate bugs, found in
sequence because fixing the first one is what exposed the second — freezing
the estimate (attempt 3) made it stop flickering, which is when the user
confirmed the *frozen* number itself didn't match the song's actual length.
Attempt 4 (ID3v2 tag accounting, below) is flashed as of 2026-07-23 but
**not yet confirmed on hardware.**

**Background:** `AudioMgr::durationSec()` (`firmware/src/audio/audio_manager.cpp`)
has never parsed an ID3/Xing header for the real duration. It estimates:
`decoded seconds so far × file size ÷ compressed bytes consumed so far`
(exact for CBR, should converge for VBR). The original hypothesis was that
this raw instantaneous ratio is too noisy frame-to-frame — SD reads land in
bursts (`consumed` jumps, then sits flat) while decoded output grows
smoothly, so the ratio swings, worst early in a track when the denominator
is small.

**First fix attempt (superseded):** added a low-pass filter (90/10
exponential smoothing) plus a warm-up threshold, reset per track. Confirmed
insufficient on hardware.

**Root cause found by re-reading the call pattern, not by guessing again:**
`AudioMgr::durationSec()` is called every UI frame — 60Hz, from
`main.cpp`'s `uiTask` (`UI::setProgress(AudioMgr::positionSec(),
AudioMgr::durationSec())`) — but `positionSec()`, the ratio's *numerator*,
only advances once per whole **decoded** second (integer division of
`meter.bytes` by the byte rate). The *denominator*, `curFile.position()`
(compressed bytes consumed), keeps growing continuously as SD reads land
throughout that ~1-second window. So within any single decoded second, the
ratio `posSec * fileSize / consumed` sawtooths downward as `consumed` grows
against a frozen `posSec`, then snaps back up when the next second ticks
over — a real, deterministic ~1-second-period oscillation, not noise. The
first fix's 90/10 filter, reapplied on every 16ms frame, decays to near-zero
memory within ~150ms — far shorter than the 1s period it needed to average
across — so it tracked the sawtooth almost exactly instead of damping it.
That matches the user's report precisely: the ratio itself is in the right
ballpark ("the estimate is correct"), but the display never settles
("changing... not static").

**Second fix attempt (flashed, confirmed still broken by the user):** only
take a new sample when `positionSec()` actually advances (once per decoded
second) instead of every UI frame, to remove the intra-second sawtooth by
construction. User's follow-up report: still not static.

**Third fix attempt — the actual root cause was the design, not the
sampling.** Both prior attempts kept the same premise: durationSec() is a
*live* value, continuously recomputed and refined for as long as the track
plays. That premise is wrong. A song's duration is fixed — it doesn't
change over time, so nothing about *displaying* it needs to keep updating.
The estimation method genuinely can't produce a perfect number in one shot
without parsing MP3 metadata (no ID3 `TLEN`, no Xing/VBRI frame-count
parsing — deliberately out of scope, see the README's Audio Pipeline
section), but there's no requirement that an imperfect estimate be
continuously re-sampled forever. Whatever residual noise is in the
sampling (never fully diagnosed — see the two abandoned root-cause threads
below), it stops being visible the moment the number simply isn't
recalculated anymore.

**Fix (attempt 3):** `durationSec()` now computes the estimate **once per
track** — after waiting for a larger, more reliable sample (`consumed >=
64KB` and `posSec >= 2`) — caches it in a `frozen` static, and returns that
cached value unconditionally afterward. Reset only on a `currentIdx` change
(new track). Before the freeze point, returns 0 ("0:00 / 0:00" briefly at
track start). **Confirmed this stopped the flickering** — user's follow-up:
the frozen number no longer changes, but it's the wrong number.

---

### Attempt 4: the frozen value itself was wrong — ID3v2 tag not accounted for

**Root cause:** `curFile.position()` (the ratio's denominator, "bytes
consumed") counts *every* byte physically read from the file, including any
ID3v2 tag at the front — helix silently skips the tag via its frame sync
without reporting how much it skipped, so `positionSec()` (the numerator,
decoded audio) doesn't start counting until real audio frames begin. Tags
carrying embedded album art (common on MP3s whose *source* files were never
stripped — this project's `prescale_art` tool writes a separate `.art`
file, it doesn't touch the original MP3's tag) can run tens to hundreds of
KB. That inflates "consumed" relative to actual decoded audio and
systematically biases the ratio toward too-short, regardless of when or how
often it's sampled — which is exactly why attempts 1–3 (all focused on
*when* to sample) never touched it.

**Fix:** `doPlay()` (`firmware/src/audio/audio_manager.cpp`) now reads the
first 10 bytes on file open and checks for the `"ID3"` magic. If present, it
decodes the syncsafe 4-byte size field (7 bits/byte, per the ID3v2 spec) to
get `id3TagSize`, then seeks back to 0 before handing the file to the decode
pipeline (which still needs to see and skip the tag itself). `durationSec()`
now computes the ratio using `consumed - id3TagSize` over
`curFileSize - id3TagSize` instead of the raw file-relative byte counts, so
the tag no longer dilutes the audio-only bitrate estimate.

**Known gap, not implemented:** this only handles an ID3v2 tag at the
*front* of the file. A trailing ID3v1 tag (fixed 128 bytes, ancient format)
or APEv2 tag at the *end* would very slightly inflate `curFileSize` the same
way — not corrected for, but 128 bytes is noise-level next to a multi-MB
file, unlike a multi-hundred-KB ID3v2 tag, so this wasn't judged worth the
complexity. Revisit only if durations are still measurably short after this
fix.

**If STILL wrong after this fix**, the value literally cannot change mid-
track anymore (frozen), so a wrong number now means the *one-shot
computation itself* is off — check next, in order of likelihood:

- Whether the file actually has a Xing/VBRI header the sample window is
  landing inside of, or a genuinely non-uniform bitrate profile (e.g. a
  quiet intro encoded at a much lower bitrate than the rest of a VBR file) —
  the 2-second/64KB sample point is still just an extrapolation from
  whatever the first few seconds of *audio* happen to look like, which for
  true VBR isn't necessarily representative of the whole file.
- Add a temporary `Serial.printf` in `doPlay()` logging `id3TagSize` and
  `curFileSize` per track, and one in `durationSec()`'s freeze branch
  logging `posSec`, `audioConsumed`, `audioTotal`, and the resulting
  `frozen` value — compare against the file's actual known duration (e.g.
  from a PC media player) rather than guessing further from source alone.
- A cross-task read race: `curFile` is opened/read/closed exclusively by
  the audio task, but `durationSec()` runs on the UI task and reads
  `curFile.position()`/`curFileSize`/`id3TagSize` without synchronization.
  Nothing in this codebase currently protects that.
