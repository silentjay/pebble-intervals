# Pebble Interval Timer — Design

2026-11-25

## Intent

A personal interval timer for a Pebble 2 HR (diorite), used for warmup sequences
before a main workout and a short morning stretch routine. The watch shows the
current period (work or rest), time remaining, and — during rest — what is
coming up next. Each step can carry a black-and-white picture of the movement.

Routines are authored on the phone, not on the watch. Pictures come from the
phone's photo storage and are converted to 1-bit art for the watch.

## Goals

- Pick a routine from a list on the watch and run it without touching the phone
  again.
- Per period: period type, step name, countdown, and step picture.
- Rest periods show the next step's name and picture ("up next").
- Buzz on every period transition so the watch does not need to be watched.
- Pause, resume, skip forward, end session.
- Author routines on the phone: name, ordered steps (name, work seconds, rest
  seconds, optional picture), with an exact 1-bit preview of each picture.
- Pictures work with no laptop involved after the app is installed.

## Non-goals (v1)

- Animated pictures. Still images only; animation multiplies the memory budget.
- Repeats, rounds, or nested blocks. Routines are straight lists.
- Heart rate, workout history, calories, syncing to other services.
- Editing routines on the watch.
- Color, or any platform other than `diorite`.

## Anti-goals

- Must not require the phone during a session once it has started.
- Must not lose a session because a picture transfer failed: a routine always
  runs, at worst text-only.
- Must not silently drop routines or pictures when storage is full — the phone
  builder refuses the save and says why.

## Hardware constraints (verified)

- Display: 144×168, 1-bit (black and white).
- Persistent storage: 4096 bytes total, 256 bytes maximum per key.
- App size: 64 KB code + heap on diorite; comparable apps report roughly 40 KB
  free heap.
- No runtime app resource updates. Art cannot be installed onto the watch after
  the app is built, and 4 KB of storage cannot hold a routine's worth of
  pictures (one 144×168 1-bit image is ~3 KB raw).

## How it works

### Watch app — C SDK, target `diorite`

Three windows:

1. **Menu** — routine names read from persistent storage. Up/Down scroll,
   Select starts, Back exits.
2. **Sync** — only when the chosen routine has pictures. Requests them from the
   phone and shows progress ("sending pictures 3/10"). If the phone does not
   respond within ~3 s, offer "start without pictures". There is an overall 8 s
   deadline; pressing Select starts immediately with whatever arrived. A stall
   never traps the session at "3/10".
3. **Session** — the timer itself.

Session states: `WORK`, `REST`, `PAUSED`, `DONE`.

- Work screen: period label, step name, MM:SS countdown, picture large.
- Rest screen: period label, next step name, "UP NEXT", countdown, next step's
  picture smaller.
- Trailing rest is skipped: after the last work period the session ends.
- Buttons: Select toggles pause, Down skips to the next period, Back ends the
  session.
- Timing uses `time()` deltas against a stored period end time, not tick
  counting, so pause and backgrounding cannot drift. A 1 s tick service only
  redraws.
- Buzz: short double at work start, one long at rest start, triple at the end.

### Phone side — two parts

**A. Config page** — a single static HTML page with inline CSS/JS, no framework
and no build step, hosted at a URL and opened from the Pebble app's settings
action. It:

- edits routines and steps;
- takes a picture from the Android photo picker, crops to 72×84 preserving
  aspect, thresholds to 1-bit with an adjustable cutoff, and shows an exact 1:1
  preview and the exact byte cost;
- enforces two budgets: watch storage for routine definitions (≤ 3 KB) and
  session art memory (≤ 12 KB), refusing saves that exceed them;
- on save, hands the whole payload (routines + pictures) back to PebbleKit JS
  by navigating to `pebblejs://close#<encoded payload>`.

The page does not talk to the watch. It cannot: it is a webview with its own
origin and its own `localStorage`. Its only exit is the close URL.

**B. PebbleKit JS (`src/pkjs/index.js`)** — runs in the Pebble app. It:

- receives the payload in the `webviewclosed` handler, decodes it, and stores
  routines and pictures in its own `localStorage` (which survives after the
  page closes and is the phone's source of truth);
- answers the watch: pushes routine definitions on request, and serves picture
  data on request during a session's Sync window;
- re-sends the routine set whenever the watch asks, so the phone never needs a
  "pending" queue.

### Data flow

- **Config close payload**: `{routines: [...], art: [...]}`, JSON, with each
  picture RLE-encoded (run of same-colour pixels) and hex-encoded so that
  `encodeURIComponent` does not inflate it. A 10-picture routine lands around
  6–10 KB of URL, well inside Android's intent limits. RLE is decoded in JS;
  the watch always receives raw rows.
- **Routines**: phone → watch after a save and whenever the watch asks on
  launch; stored in the watch's persistent storage as compact binary, one key
  per routine (chunked when over 256 bytes). Cap: 8 routines, 15 steps each.
  Strings are length-prefixed and capped (routine name 20 bytes, step name
  20 bytes). The watch keeps the last set it received, so the menu works with
  the phone away.
- **Pictures**: never stored on the watch. At session start the watch requests
  each picture for the selected routine; PebbleKit JS replies with packed 1-bit
  rows (72×84, stride 9 bytes, MSB first, 1 = white, 0 = black); the watch
  copies them into a 1-bit `GBitmap` and keeps it in RAM for the session.
  The rest screen's thumbnail (36×42) is derived on the watch by 2×2 majority
  downsampling at sync time, so only one picture per step crosses the wire.
- **Budgets**: 12 pictured steps per routine (12 × 756 B ≈ 9 KB).
- **Fallbacks**: no phone at start → text-only session. Phone drops mid-sync →
  start with whatever arrived. Nothing about a failed transfer is persisted.

## Decisions made (not open)

- C SDK rather than Alloy: Alloy does not support diorite.
- Pictures live in RAM, not storage, because 4 KB cannot hold them.
- One picture per step at a fixed 72×84; the 36×42 rest thumbnail is derived on
  the watch rather than sent, because the C SDK cannot scale bitmaps at draw
  time and a second payload would double the budget.
- No compression for the phone→watch transfer (raw rows); RLE only inside the
  config-close URL, where size matters.
- Routines are re-pushed in full on every request; no pending-sync queue.
- Config page is a static page hosted at a URL (GitHub Pages is the default),
  because PebbleKit JS only runs inside the Pebble mobile app.
- Android is the target phone.

## Left to the builder

- Window layout and typography within the 144×168 screen.
- AppMessage chunk size, chosen at runtime from
  `app_message_inbox_size_maximum()`, and the transfer protocol framing.
- Exact persistent-storage encoding (positions and sizes of fields).
- Thresholding and downscaling algorithm for the phone converter; a
  luminance threshold with optional 2× pre-downscale is sufficient.
- Where the config page is hosted.
- Exact RLE framing inside the close URL.

## Findings from the build

1. **Rebble webview file picker and close-URL size** — still unverified. The
   `webviewclosed` → PebbleKit JS `localStorage` path is documented and the
   payload stays around 10 KB (RLE + hex), but the Android file picker inside
   the Rebble webview must be confirmed on the phone.
2. **AppMessage limits** — the inbox is opened at
   `app_message_inbox_size_maximum()`. Pictures travel as two 378-byte
   messages; routine sets as 128-byte chunks. Transfer rate is still to be
   measured on hardware; the 8 s deadline is the backstop.
3. **1-bit `GBitmap`** — the SDK creates a blank 1-bit bitmap and exposes its
   stride through `gbitmap_get_bytes_per_row()`, so raw packed rows are copied
   straight in. Confirmed by reading the installed SDK headers; drawing still
   needs the on-watch check.

## Change from the original design

Pictures are **72×84** (stride 9), not 96×112, and the rest thumbnail is
36×42. At 96×112 the picture, phase label, step name and a 42 px countdown do
not fit in 168 px. Art budget follows: 12 pictured steps ≈ 9 KB.
