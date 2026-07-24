# Handoff — True 60fps spin + legible title text

Both items below are now **implemented and flashed** (2026-07-23, same COM10
board as the WROVER bring-up). Serial boot log confirms no crash and healthy
heap margins. Neither has been **visually confirmed on the panel** in this
session — no camera/eyes on the device — so treat the "looks right" claims
below as pending, not closed.

---

## 1. True 60fps vinyl spin

**Status:** implemented, flashed. Visual smoothness NOT eyeballed.

Added to `firmware/src/lv_conf.h`:

```c
#define LV_DISP_DEF_REFR_PERIOD  16   // ms — match UI_REFRESH_MS for a real 60fps redraw
```

This was previously unset, silently falling back to LVGL's default 30ms
(~33fps) — `vinyl_angle` advanced at 60Hz but only ~33 of those steps/sec
ever reached the screen. Zero RAM cost (a timing constant, not a buffer).
Builds clean on both `esp32dev` and `esp32dev-debug`.

**Still open:** confirm on the panel that the spin now looks smoother, not
just faster-but-still-choppy. If a USB power meter is available, comparing
idle-playback current at 30ms vs. 16ms would answer the "is the power cost
noticeable" question raised in the original analysis — still unmeasured.

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
