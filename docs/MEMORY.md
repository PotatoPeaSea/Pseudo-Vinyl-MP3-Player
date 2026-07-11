# Memory Findings — ESP32-WROOM-32 Bring-up

Findings, issues, and fixes from getting Bluetooth A2DP, the SD library, and
the LVGL UI to coexist on the WROOM-32. Everything here comes from hardware
bring-up on the `feature/bt-only-lowmem` branch (July 2026), plus the
RAM-minimization pass on `feature/ram-squeeze` (see the section at the end).

## The core problem

The ESP32-WROOM-32 has **520KB of SRAM, no PSRAM**. After the ROM, Wi-Fi/BT
controller reservations, static data, and FreeRTOS overhead, the Arduino
sketch starts with roughly **~250–290KB of free heap** — and that must hold
*everything* dynamic:

| Consumer | Approx. cost | Notes |
|---|---|---|
| Classic BT + A2DP (Bluedroid) | **~120KB to initialize**, ~108KB retained | The single biggest consumer; non-negotiable for BT audio |
| A2DP connection handshake | **~50KB contiguous, transient** | Only ~5KB retained once connected — but the 50KB must be available *as one block* at connect time |
| SD / FATFS mount | ~80KB retained while mounted | Also badly fragments the heap (largest free block drops to ~20KB) |
| LVGL draw buffers | 9.6KB (was 28.8KB) | 10 rows × 2 buffers, DMA-capable internal RAM |
| LVGL widget tree | ~700 bytes per song-list button | Scales with library size |
| BT audio ring buffer | 10KB (8KB stuttered, 16KB broke the budget) | ~58ms of 44.1kHz stereo PCM |
| Bluedroid media path (post-connect) | ~28KB, allocated AFTER `state=2` | Must not race FATFS; SD mount gates on `isStreaming()` |
| MP3 decode (helix) | ~25KB | Chosen because it runs from internal SRAM |
| Task stacks | 3KB + 8KB + 6KB | input / ui / audio — each needs a *contiguous* block |

Two distinct failure modes kept recurring:

1. **Total exhaustion** — an allocation simply fails (or Bluedroid aborts
   during init).
2. **Fragmentation** — plenty of total free heap, but no single block large
   enough. `ESP.getFreeHeap()` looks fine; `ESP.getMaxAllocHeap()` tells the
   real story. FATFS is the main fragmenter.

## Issues found and fixed

### 1. Bluetooth OOM boot loop (black screen on every boot)

**Symptom:** Device boot-looped before the UI rendered — appeared as a dead
display. **Cause:** SD was mounted before Bluetooth started. FATFS retained
~80KB, leaving ~82KB free; Bluedroid needs ~120KB to initialize and aborted.
**Fix:** Reordered boot so BT initializes first, while ~190KB is still free
(`ccbe576`).

### 2. No room for a second audio path — wired I2S output removed

The PCM5102 wired output couldn't fit next to the BT stack. Output is now
**Bluetooth-only**; the I2S pipeline and its buffers were deleted
(`ccbe576`). The pins were reclaimed for the rewired display/SD (see
`config.h` pin map).

### 3. LVGL buffers and BT ring buffer shrunk

- LVGL draw buffers: 28.8KB → **9.6KB** (10 rows × 2 buffers), with a static
  single-buffer fallback if even that allocation fails
  (`display_manager.cpp`).
- BT PCM ring buffer: 16KB → 8KB (~46ms of audio) in `config.h`.
  **Reverted to 16KB** after hardware playback stuttered — see "BT ring
  buffer" under the ram-squeeze section.

### 4. Song list could NULL-deref LVGL allocations

Building the song list ran the heap dry, and LVGL (which uses system malloc)
returned NULLs that were dereferenced. **Fixes:** `MAX_SONGS` cap (20, later
15), plus a runtime guard in `UI::setSongList` that stops building buttons
when `getMaxAllocHeap()` drops below a 20KB floor — the list truncates
gracefully instead of crashing (`ccbe576`, `cce2c12`).

### 5. Silent input-task failure — all controls dead

**Symptom:** Neither hardware buttons nor the serial debug console did
anything. **Cause:** The input task's 4KB stack could not be allocated from
the heap left fragmented by BT + FATFS (~20KB largest block), and the
`xTaskCreatePinnedToCore` result was unchecked, so the failure was invisible.
**Fixes** (`cce2c12`):

- Create the **input task first**, before the larger UI/audio tasks fragment
  what's left.
- Trim stacks: input 4096→3072, audio 8192→6144 (UI stays 8192).
- Check and loudly log every task-creation result.
- `MAX_SONGS` 20→15 for extra headroom.

### 6. A2DP handshake starved by FATFS fragmentation — SD mount now deferred

**Symptom:** With SD mounted at boot, the speaker connection never completed
even though BT initialized fine. **Cause:** The A2DP *connection handshake*
transiently needs a **~50KB contiguous block**; after a FATFS mount the
largest free block is ~20KB. Once connected, A2DP retains only ~5KB — the
spike is temporary. **Fix:** SD mount + song scan are **deferred until a
speaker is connected** (`maybeMountStorage()` in `main.cpp`, run on the UI
task so LVGL access stays single-threaded). Boot order is now:

```
display/UI → input → BT init + A2DP start → tasks → (speaker connects) → SD mount + song scan
```

### 7. Bluetooth connection fixes (behavioral, found while debugging memory)

- **`set_auto_reconnect(false)`** — with a saved address the library hammers
  a direct connect (×1000 retries) and never runs an inquiry scan, so the UI
  device list stayed empty. Discovery + the `ssid_callback` now handles
  auto-connect while keeping the scan list populated.
- **`set_ssp_enabled(true)`** — modern speakers (JBL etc.) require Secure
  Simple Pairing; without it the ESP32 falls back to legacy PIN pairing and
  the connection fails.
- `setTarget()` skips redundant NVS writes; connection-state changes log
  heap (`free` / `largest`) for diagnosis.

## Instrumentation

Heap is logged at every boot stage and every BT state change as
`free=<total> largest=<biggest contiguous block>`. **Always watch `largest`,
not just `free`** — every fragmentation bug above looked healthy by total
free heap. Current key checkpoints: boot start, after display+UI, before/after
BT+audio, before task creation, at SD mount, after song-list build.

## Current state (as of `feature/bt-only-lowmem`)

- Boots stably; all three tasks verified started; BT connects to a speaker.
- ~33KB free / ~20KB largest block after BT init; ~22KB free at idle.
- Song library capped at 15 tracks; album art capped at 120×120 RGB565
  (28.8KB) scaled onto the 90px vinyl label — a full 240×240 frame (113KB)
  can never fit.

## Known remaining risks

- **Headroom is razor-thin.** ~22KB free at idle means any new feature must
  be budgeted; assume fragmentation halves what's usable.
- **Playback under load is not fully verified** — decode + ring buffer + UI
  + a populated song list all running at once is the worst case.
- If the speaker disconnects and reconnects *after* SD is mounted, the
  handshake's ~50KB transient need may fail against the post-FATFS heap
  (~20KB largest). Reconnect behavior needs testing; unmounting SD around
  reconnects may become necessary.
- A song library over 15 tracks (or long filenames/metadata) will truncate.

## RAM-minimization pass (`feature/ram-squeeze`, July 2026)

A deliberate everything-for-RAM pass; UX regressions (slower screen changes,
slower play-start after a full stop, smaller art) were accepted by design.
Not yet verified on hardware.

### What changed

| Change | Saving | UX cost |
|---|---|---|
| Arduino loopTask deleted at end of `setup()` (`vTaskDelete(nullptr)`) | ~8KB stack + TCB | none — all work is in our tasks |
| **Lazy screen lifecycle**: only the active screen's LVGL tree exists; built on show, deleted on switch | ~10–15KB idle (song list alone is ~700B × 15 buttons) | screen switches rebuild widgets (slower, no fade) |
| Album art freed when leaving Now Playing; re-read from SD on return | up to 16.2KB while browsing | SD read on each return to Now Playing |
| `ART_MAX_SIDE` 120 → **90** (the vinyl label is 90px — larger was downscaled anyway) | 28.8KB → 16.2KB max art buffer | old 120px `.art` files rejected; re-run prescale_art |
| Single song library vector; AudioMgr/UI hold pointers (were full copies) | ~2× library cost | none |
| `SongInfo` slimmed: title derived from filename on demand, `artist` dropped (was always "Unknown") | ~1KB at 15 songs | artist line gone from Now Playing |
| ~~Helix decoder freed on full stop / playlist end~~ **REVERTED** — the "low risk" realloc failure happened on hardware (see "Play-start helix allocation" below); decoder is now allocated once at boot and kept forever | ~~~25KB while idle~~ 0 | none (25KB is a permanent budget line) |
| Single LVGL draw buffer (10 rows) instead of double | 4.8KB | slower rendering (render waits for SPI flush) |
| Static display-buffer fallback removed (was always-resident .bss) | 7.7KB | none — `Display::init` runs first, alloc can't fail |
| LVGL: basic theme (default theme heap-allocates dozens of styles), circle cache 4 → 2, unused widgets/fonts off | ~2–4KB heap + flash | slightly plainer unstyled-widget look |
| BT discovery list capped at `MAX_BT_DEVICES` (8) | unbounded → bounded | busy environments show first 8 devices |
| Per-frame UI setters early-return when unchanged | kills 60Hz `lv_label_set_text` realloc churn (fragmentation!) | none |
| `CORE_DEBUG_LEVEL` 3 → 1 | mostly flash | fewer IDF logs |

Also fixed while in there: screen switches are now deferred to the UI task
(`pendingScreen`), because with delete-on-switch a click callback must never
delete the screen it is running inside, and the input task must never touch
the LVGL tree directly.

### Deliberately NOT shrunk

- **BT PCM ring buffer: 10KB** (~58ms cushion). The squeeze to 8KB (46ms)
  stuttered on hardware once real playback worked; the first fix attempt
  (16KB) overshot the budget and killed the stream entirely (next section).
  The data callback and `writeAudio` log underruns / send-timeouts
  (rate-limited) so buffer-health is measurable, not guessed.

### The heap budget must close: 16KB buffer + resident decoder killed the stream

**Symptom:** After the resident-decoder fix and a 16KB ring buffer, the
telemetry showed the Bluedroid media path made exactly ONE data request
(512 bytes, zero-filled) after `audio state=2` and then went permanently
silent — play produced only `ringbuf send timeout (consumer stalled)`. On
the same run: `[UI] Song list truncated at 0/15` — the 20KB list floor
could never pass.

**Cause:** The two fixes added **+33KB of permanent residents** (decoder
25KB + buffer growth 8KB) without rebalancing. Media start then ran with a
12.3KB largest block — and it *raced the FATFS mount*, since both were
triggered by the same connect event. Bluedroid's media path allocates
*after* the connect (observed: ~28KB between `state=2` and steady state);
starved of that, it stalls with no error at `CORE_DEBUG_LEVEL=1` — the
audio state still says "started".

**Fixes:**

- **Sequencing:** the SD mount now waits for `BtMgr::isStreaming()` (audio
  state STARTED, tracked from the audio-state callback) instead of
  `isConnected()` — Bluedroid finishes ALL its allocations against a clean
  heap before FATFS fragments it.
- **Rebalance:** ring buffer 16KB → 10KB; song-list floor 20KB → 11KB
  (largest sits ~12–16KB in steady state now, and LVGL's per-button allocs
  are small — the old floor assumed a roomier heap).
- Underrun telemetry only counts when the producer wrote within the last
  second, so idle silence-streaming doesn't spam the log — and a healthy
  idle consumer is now visible as the *absence* of noise.

**Lesson:** every "fix" that pins memory must re-close the whole budget.
The order of allocation matters as much as the amount: same totals, but
media-start-then-FATFS works where the race dies. Connected ≠ streaming;
gate on the state you actually need.
- **Task stacks unchanged** (3K/8K/6K) — no hardware high-water-mark data to
  justify trims; a stack overflow is a crash, not a slowdown.
- BLE controller memory: already released — the ESP32-A2DP library runs the
  controller in `ESP_BT_MODE_CLASSIC_BT` and calls
  `esp_bt_controller_mem_release(ESP_BT_MODE_BLE)` itself.

### Crash found in hardware testing: OOM during screen switch at play start

**Symptom:** `StoreProhibited` (write to `0x14`) on Core 1 the moment a song
started, in `lv_obj_class_create_obj` ← `lv_label_create` ← `doShowScreen`.
**Diagnosis (addr2line + LVGL source):** LVGL 8 does not check the
`lv_mem_realloc` of a parent's child array; offset `0x14` is exactly
`children[5]` through a NULL pointer — the heap was fully exhausted while
building the Now Playing screen. Three costs coincided:

1. `doShowScreen` originally built the new screen *before* deleting the old
   one — song list (15 buttons) + new tree resident together;
2. `AudioMgr::play()` had just run the SD open + helix decoder init (~25KB)
   — and it ran directly on whichever task called it (here the 3KB input
   task, also a stack-overflow and cross-core race risk);
3. album art loading had no heap guard and would have been next.

**Fixes:**

- `doShowScreen` now deletes the outgoing tree **before** building the new
  one — peak is max(old, new), not the sum. LVGL can't have its active
  screen deleted, so a bare placeholder screen (~150B) bridges the gap (one
  blank frame). Heap is logged at every switch.
- `AudioMgr::play()/stop()` are now deferred requests executed by
  `AudioMgr::loop()` on the audio task (6KB stack, same task as the decode
  loop — no cross-core pipeline races, nothing heavy on the input task).
- `Storage::loadArtFile` skips the art (with a log) when
  `getMaxAllocHeap() < artSize + 12KB` — a missing cover beats an OOM crash.

**Lesson:** LVGL 8 with `LV_MEM_CUSTOM` malloc does NOT survive allocation
failure — `lv_obj_class_create_obj`'s realloc is unchecked. Any LVGL tree
construction must be preceded by enough guaranteed headroom; guards like the
song-list `HEAP_FLOOR` are load-bearing, not defensive fluff.

### Silent playback #2: A2DP media-start starved by the play-start heap crunch

**Symptom:** Speaker connected, decode running, but no sound; serial spammed
`libhelix - Could not write result to out: 0 of 4608 written` — the PCM ring
buffer filled once and never drained. (Distinct from silent-playback #1, the
unwired `meter.out` pipeline link, fixed just before.)

**Diagnosis (ESP32-A2DP library source):** The A2DP stack only pulls from the
data callback after a media-start handshake (`CHECK_SRC_RDY` → `START`), and
the library triggers that from a **10-second heartbeat timer** — not from the
connect event. Play was pressed within that window, and `doPlay()` (helix
decoder ~25KB + Now Playing screen build) had already crushed the heap to
`largest=12276` by the time the first handshake ran. Bluedroid allocates its
media task + SBC encode buffers at START; the attempt fails, the library
falls back to IDLE, and every 10s retry fails against the same starved heap
— permanently, and invisibly at `CORE_DEBUG_LEVEL=1` (the failure logs at
INFO level).

**Fixes:**

- `connectionStateChanged` now calls
  `esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY)` the moment the
  connection completes — the handshake runs while the largest block is still
  ~73KB, before any play can fragment it. The heartbeat stays as the retry.
- `set_on_audio_state_changed` registered with a heap log: if
  `[BT] audio state=2 (streaming)` never appears after a connect, this
  failure mode is back.

**Lesson:** Bluedroid allocates lazily — connect ≠ ready to stream. Every
deferred allocation inside the BT stack is a landmine on a heap this tight;
force them to happen at the moments heap is known-good (same principle as
the deferred SD mount, but in the opposite direction).

### Play-start helix allocation failed → watchdog reboot

**Symptom:** `libhelix - allocateation failed for 5120 bytes` at play start,
then a task-watchdog abort ~5s later (`CPU 0: audio` starving `IDLE0`) and
reboot. **Cause:** The ram-squeeze pass freed the helix decoder while idle
(rated "low risk" to re-allocate — wrong). At play start, `decoder.begin()`
on the audio task raced the Now Playing screen build on the UI task for the
last large blocks (largest was 29.7KB before, 12.3KB after) and lost one
5KB chunk. Playback then proceeded with a half-initialized decoder, whose
internal retry loop never yields — tripping the watchdog.

**Fixes:**

- Helix decoder is allocated **once in `AudioMgr::init()` at boot** (heap
  ~98KB largest) and never freed. The idle-RAM saving is given back: playing
  music is the device's core function, so its ~25KB is a permanent budget
  line, not a reclaimable one.
- `doPlay()` checks `decoder.begin()`'s result and aborts the track cleanly
  on failure instead of feeding a broken decoder. (Per-track `begin()` is
  still a free+realloc of the ~25KB — `CommonHelix::begin` calls `end()` on
  an active decoder — but re-filling just-freed holes has proven reliable
  even at 12KB largest; it's the *first* allocation under pressure that
  loses races.)

**Lesson:** "We can re-allocate it later" is only true if *later* has a
known heap state. Later turned out to be the single worst moment in the
whole runtime (decoder + screen build + art, three tasks racing). Allocate
core-function memory at boot, when the heap is deterministic.

### Static RAM (build-time, `pio run -e esp32dev`)

- Before: 59,892 bytes (18.3%) static, 1,638,825 flash.
- After: **51,996 bytes (15.9%)** static, 1,605,833 flash — **−7.9KB static
  RAM** (mostly the removed display fallback buffer + trimmed LVGL .bss).
- The bigger wins are on the heap at runtime (loopTask stack, three screens'
  widget trees, duplicate song vectors, theme styles, second draw buffer,
  off-screen art, idle decoder) and show up in the `free=/largest=` boot
  logs, not the build-time number.
- Boot now builds ONE screen instead of four, so the heap is also healthier
  at the moment Bluedroid initializes.
