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
   phone and shows progress ("sending pictures 3/10"). If no phone responds
   within a few seconds, offer "start without pictures".
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

### Phone side — PebbleKit JS config page

A single static HTML page with inline CSS/JS, no framework and no build step.
It is opened from the Pebble app's settings action and:

- keeps the source-of-truth routine list in `localStorage`;
- edits routines and steps;
- takes a picture from the Android photo picker, crops to fit within 144×168
  preserving aspect, thresholds to 1-bit with an adjustable cutoff, and shows
  an exact 1:1 preview and the exact byte cost;
- enforces two budgets: watch storage for routine definitions (≤ 3 KB) and
  session art memory (≤ 16 KB), refusing saves that exceed them;
- pushes routine definitions to the watch;
- serves pictures on request while the watch is in its Sync window.

### Data flow

- **Routines**: phone → watch at save time; stored in persistent storage as
  compact binary, one key per routine (chunked when over 256 bytes). Cap: 8
  routines, 15 steps each. Strings are length-prefixed and capped (routine name
  24 bytes, step name 24 bytes).
- **Pictures**: never stored on the watch. At session start the watch requests
  each picture for the selected routine; the phone replies with packed 1-bit
  rows (stride = ceil(width/8), one bit per pixel, MSB first); the watch copies
  them straight into a 1-bit `GBitmap` and keeps it in RAM for the session.
  Raw rows are used rather than compression: simplicity, and the transfer is a
  few KB.
- **Fallbacks**: no phone at start → text-only session. Phone drops mid-sync →
  start with whatever arrived. Nothing about a failed transfer is persisted.

## Decisions made (not open)

- C SDK rather than Alloy: Alloy does not support diorite.
- Pictures live in RAM, not storage, because 4 KB cannot hold them.
- No compression for picture transfer; raw packed rows.
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

## Spike before building (unknowns that could change the plan)

1. Does the Rebble Android app's config webview allow `localStorage` and
   `<input type="file">`? If not, the phone builder needs a different host
   (e.g. a standalone web app plus manual import), which changes the phone side.
2. What is diorite's actual AppMessage inbox maximum, and how fast can ~8 KB of
   picture data move?
3. Does a `GBitmap` of format `1Bit` accept raw packed rows via
   `gbitmap_get_data()` with the stride the watch computes?

## Testing

- Watch: build for diorite and install over the developer connection; the state
  machine is exercised by pausing, skipping and letting periods expire.
- Phone: the converter and the routine-to-period expansion are pure functions,
  checked with a small Node script (known input → expected packed bytes and
  period list).
