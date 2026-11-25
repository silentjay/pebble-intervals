# Pebble Interval Timer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Pebble 2 (diorite) interval timer with phone-authored routines and per-step 1-bit pictures streamed from the phone at session start.

**Architecture:** A C watch app (menu → optional art sync → session) plus a PebbleKit JS service on the phone that owns the data. A static config page edits routines and converts pictures; it hands the payload back through the `pebblejs://close#` URL, which PebbleKit JS stores in its own `localStorage` and serves to the watch over AppMessage. Watch-side logic is split into two host-testable pure C modules (`store`, `session`) and Pebble-only UI/wiring.

**Tech Stack:** Pebble C SDK 4.9.148 (`diorite`), PebbleKit JS, vanilla HTML/CSS/JS (no framework, no build step), Node's built-in `node:test` for the JS tests, `cc` + ASan for the C host tests.

**Spec:** `docs/superpowers/specs/2026-11-25-pebble-intervals-design.md`

## Global Constraints

- Target platform `diorite` only. Display 144×168, 1-bit.
- Bit polarity in packed data: **0 = black, 1 = white** (SDK convention).
- Watch persistent storage: 4096 bytes total, 256 bytes max per value. Budget use ≤ 3 KB.
- Caps: 8 routines, 15 steps per routine, routine name 20 bytes, step name 20 bytes, 12 pictured steps per routine.
- Pictures: fixed 96×112, stride 12 bytes, raw packed rows. Rest thumbnail 48×56 derived on the watch by 2×2 majority downsample.
- Session art memory budget: 20 KB. Overall art-sync deadline: 8 s; first-response timeout: 3 s.
- Timing uses `time()` deltas, never tick counts. Trailing rest is skipped.
- Phone target: Android. SDK 4.9.148 is already installed; `pebble` CLI is on PATH.
- No dependencies beyond the Pebble SDK and Node's built-ins.
- Buzz patterns: short double at work start, one long at rest start, triple at session end.

## Review Focus

- Phone absent or Bluetooth off when a routine with pictures is started → must fall back to a text-only session instead of hanging on the sync screen.
- Step with `rest = 0`, and the last step's rest → must not create a zero-length period or an extra buzz; routine moves on or finishes.
- Names longer than 20 bytes, or non-ASCII input → truncated safely, never a buffer overrun.
- More routines/steps or art than the caps allow → save refused with a visible reason; the watch's existing data is left intact.
- Art transfer that stops mid-way or delivers a corrupt chunk → session starts without that picture rather than crashing or blocking.

---

### Task 1: Scaffold the app and build for diorite

**Files:**
- Create: `package.json`
- Create: `wscript`
- Create: `src/main.c`
- Create: `.gitignore`
- Test: `pebble build`

**Interfaces:**
- Consumes: nothing.
- Produces: a buildable app named `pebble-intervals`, displaying a placeholder window on diorite.

- [ ] **Step 1: Write `package.json`**

```json
{
  "name": "pebble-intervals",
  "author": "John",
  "version": "1.0.0",
  "keywords": ["pebble-app"],
  "private": true,
  "dependencies": {},
  "pebble": {
    "displayName": "Intervals",
    "uuid": "3f6f0d0e-7f3a-4c0f-9a2e-6c1b0a5d4e21",
    "sdkVersion": "3",
    "targetPlatforms": ["diorite"],
    "capabilities": ["configurable"],
    "messageKeys": [
      "Cmd", "RoutineId", "StepIndex", "ChunkIndex", "ChunkCount", "PayloadLen", "Chunk"
    ]
  }
}
```

- [ ] **Step 2: Write `wscript`**

```python
top = '.'
out = 'build'
```

- [ ] **Step 3: Write the minimal `src/main.c`**

A `Window` with a `TextLayer` reading "Intervals". Standard `main()` that calls `app_event_loop()`.

- [ ] **Step 4: Write `.gitignore`**

```
build/
node_modules/
```

- [ ] **Step 5: Build**

Run: `pebble build`
Expected: `build/pebble-intervals.pbw` exists, no warnings.

- [ ] **Step 6: Install and confirm**

Run: `pebble install --phone <watch-ip>` (developer connection enabled), then launch. Expected: "Intervals" visible.

- [ ] **Step 7: Commit**

```bash
git add package.json wscript src/main.c .gitignore
git commit -m "chore: scaffold diorite app"
```

---

### Task 2: Routine encoding and persistence (`store`)

**Files:**
- Create: `src/store.h`, `src/store.c`
- Create: `test/store_test.c`
- Create: `test/run_c_tests.sh`

**Interfaces:**
- Consumes: nothing (must not include `pebble.h`).
- Produces:
  - `#define MAX_ROUTINES 8`, `#define MAX_STEPS 15`, `#define NAME_MAX 20`
  - `typedef struct { char name[NAME_MAX + 1]; uint16_t work_s; uint16_t rest_s; uint8_t flags; } Step;` with `STEP_HAS_ART 0x01`
  - `typedef struct { char name[NAME_MAX + 1]; uint8_t step_count; Step steps[MAX_STEPS]; } Routine;`
  - `int store_encode_blob(const Routine *in, int count, uint8_t *out, size_t cap);`
  - `int store_decode_blob(const uint8_t *in, size_t len, Routine *out, int max);`
  - `size_t store_routine_bytes(const Routine *r);` (encoded size, used by the save budget)

- [ ] **Step 1: Write the failing test**

Assertions in `test/store_test.c`:
- Encoding the fixture below produces exactly the hex listed.
- Decoding that hex yields 1 routine named `Warm`, 2 steps, values `("Arm",30,10,0)`, `("Leg",45,0,1)`.
- A 40-character name is truncated to 20 bytes.
- Decoding a truncated blob returns −1 and leaves `out` untouched.
- Encoding 9 routines returns −1.

Fixture blob:
```
01 02 04 57 61 72 6D 03 41 72 6D 1E 00 0A 00 00 03 4C 65 67 2D 00 00 00 01
```
Layout: `u8 count`, then per routine `u8 step_count, u8 name_len, name[]`, then per step `u8 name_len, name[], u16 work_le, u16 rest_le, u8 flags`.

- [ ] **Step 2: Run it and watch it fail**

Run: `test/run_c_tests.sh`
Expected: compile error, `store_encode_blob` undeclared.

- [ ] **Step 3: Implement `store.c`**

`store_encode_blob` walks the structs and writes the layout above, returning the byte count or −1 when `cap` is too small or `count > MAX_ROUTINES`. `store_decode_blob` validates every length against the remaining input and `MAX_STEPS`/`NAME_MAX` before copying; copy into a temporary and only assign to `out` on success.

`test/run_c_tests.sh`:

```bash
#!/usr/bin/env bash
set -e
cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I src \
   test/store_test.c src/store.c -o /tmp/store_test
/tmp/store_test
```

- [ ] **Step 4: Run tests and confirm pass**

Run: `test/run_c_tests.sh` (make executable first)
Expected: `store: ok`, exit 0.

- [ ] **Step 5: Wire persistence (Pebble-only, no host test)**

In `store.h`/`store.c` behind `#ifdef PEBBLE`, add:

```c
bool store_commit_blob(const uint8_t *blob, size_t len);
int  store_load_all(Routine *out, int max);
```

`store_commit_blob` decodes into a static `Routine tmp[MAX_ROUTINES]`, then writes `0x0001` = count and, per routine, `0x0800 + id` = header bytes and `0x1000 + id * MAX_STEPS + i` = one 26-byte step record. Delete stale keys of routines beyond the new count. `store_load_all` reads the count and records back.

- [ ] **Step 6: Build**

Run: `pebble build`
Expected: success.

- [ ] **Step 7: Commit**

```bash
git add src/store.h src/store.c test/store_test.c test/run_c_tests.sh
git commit -m "feat: routine encode/decode and persistence"
```

---

### Task 3: Session state machine (`session`)

**Files:**
- Create: `src/session.h`, `src/session.c`
- Create: `test/session_test.c` (extend `test/run_c_tests.sh`)

**Interfaces:**
- Consumes: `Routine`, `Step` from Task 2.
- Produces:

```c
typedef enum { PHASE_WORK, PHASE_REST, PHASE_DONE } Phase;
typedef struct {
  const Routine *routine;
  int step;
  Phase phase;
  bool paused;
  uint32_t end_ms;
  uint32_t paused_remaining_ms;
} Session;

void session_start(Session *s, const Routine *r, uint32_t now_ms);
// Returns true when phase or step changed (caller buzzes).
bool session_update(Session *s, uint32_t now_ms);
void session_toggle_pause(Session *s, uint32_t now_ms);
void session_skip(Session *s, uint32_t now_ms);
uint32_t session_remaining_ms(const Session *s, uint32_t now_ms);
```

- [ ] **Step 1: Write the failing tests**

In `test/session_test.c`, using a 3-step routine (`30/10`, `45/0`, `20/5`):
- `session_start` at t=0 → `PHASE_WORK`, step 0, remaining 30000.
- At t=30000 → `PHASE_REST`, step 0, changed = true.
- At t=40000 → `PHASE_WORK`, step 1, changed = true (rest 0 is skipped).
- At t=85000 → `PHASE_WORK`, step 2 (step 1's rest is skipped, no extra rest phase).
- At t=105000 → `PHASE_DONE`; the last step's 5 s rest never runs.
- Pause at t=5000 then `session_remaining_ms` at t=60000 still returns 25000; resume at t=60000 → ends at t=85000.
- `session_skip` on a rest phase jumps to the next work step; on the last work phase → `PHASE_DONE`.
- A paused session does not change phase on `session_update`.

- [ ] **Step 2: Run and watch it fail**

Run: `test/run_c_tests.sh`
Expected: compile error, `session_start` undeclared.

- [ ] **Step 3: Implement `session.c`**

Phase advance uses `end_ms` comparisons. `session_update` advances in a small loop so a long gap cannot leave the state stale, and returns whether `(step, phase)` changed. `session_toggle_pause` stores `paused_remaining_ms = end_ms - now_ms` and, on resume, sets `end_ms = now_ms + paused_remaining_ms`. `session_remaining_ms` returns 0 when paused and `PHASE_DONE`.

Add to `run_c_tests.sh`:

```bash
cc -std=c11 -Wall -Wextra -fsanitize=address,undefined -I src \
   test/session_test.c src/session.c -o /tmp/session_test
/tmp/session_test
```

- [ ] **Step 4: Run tests and confirm pass**

Run: `test/run_c_tests.sh`
Expected: `store: ok`, `session: ok`.

- [ ] **Step 5: Commit**

```bash
git add src/session.h src/session.c test/session_test.c test/run_c_tests.sh
git commit -m "feat: interval session state machine"
```

---

### Task 4: Phone image conversion (`convert.js`)

**Files:**
- Create: `src/pkjs/config/convert.js` (UMD-style: `module.exports` under Node, global under browser)
- Create: `test/convert.test.js`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `packThreshold(gray, w, h, cutoff)` → `Uint8Array` packed rows, MSB first, bit 1 = white. `gray` is one byte per pixel.
  - `fitRect(sw, sh, tw, th)` → `{sx, sy, sw, sh}` centered cover crop.
  - `rleEncode(bytes)` / `rleDecode(bytes, expectedLength)` — byte pairs `[count, value]`, count 1..255.
  - `toHex(bytes)` / `fromHex(str)`.

- [ ] **Step 1: Write the failing tests**

`test/convert.test.js` with `node:test`:
- A 8×1 row of gray `[0,255,0,255,0,255,0,255]` at cutoff 128 packs to `0x55`.
- The same row inverted packs to `0xAA` (pins polarity).
- `fitRect(200, 100, 96, 112)` → `{sx:50, sy:0, sw:100, sh:100}`.
- `rleDecode(rleEncode([1,1,1,2,9]))` returns the original.
- `fromHex(toHex([0x00,0xab,0xff]))` returns the original.

- [ ] **Step 2: Run and watch it fail**

Run: `node --test test/`
Expected: `Cannot find module '../src/pkjs/config/convert.js'`.

- [ ] **Step 3: Implement `convert.js`**

`packThreshold`: set bit when `gray[i] >= cutoff`. Row stride `(w + 7) >> 3`, rows padded with 0 (black padding, outside the drawn area).

- [ ] **Step 4: Run tests and confirm pass**

Run: `node --test test/`
Expected: 5 passing.

- [ ] **Step 5: Commit**

```bash
git add src/pkjs/config/convert.js test/convert.test.js
git commit -m "feat: 1-bit packing and RLE helpers"
```

---

### Task 5: Routine blob encoder in JS (contract test)

**Files:**
- Modify: `src/pkjs/config/convert.js` (add `encodeRoutineSet`)
- Modify: `test/convert.test.js`

**Interfaces:**
- Consumes: nothing.
- Produces: `encodeRoutineSet(routines)` → `Uint8Array`, where a routine is `{name, steps: [{name, work_s, rest_s, has_art}]}`, producing exactly the Task 2 blob layout.

- [ ] **Step 1: Write the failing test**

Assert `encodeRoutineSet([{name:"Warm", steps:[{name:"Arm",work_s:30,rest_s:10,has_art:false},{name:"Leg",work_s:45,rest_s:0,has_art:true}]}])` equals the Task 2 fixture hex.

- [ ] **Step 2: Run and watch it fail**

Run: `node --test test/`
Expected: `encodeRoutineSet is not a function`.

- [ ] **Step 3: Implement `encodeRoutineSet`**

Truncate names to 20 UTF-8 bytes without splitting a code point; `flags = has_art ? 1 : 0`; little-endian `uint16`. Throw when count > 8, steps > 15, or any duration > 65535.

- [ ] **Step 4: Run tests and confirm pass**

Run: `node --test test/`
Expected: 6 passing.

- [ ] **Step 5: Commit**

```bash
git add src/pkjs/config/convert.js test/convert.test.js
git commit -m "feat: routine blob encoder shared contract"
```

---

### Task 6: PebbleKit JS phone service

**Files:**
- Create: `src/pkjs/index.js`
- Create: `test/pkjs.test.js`
- Create: `test/fake-pebble.js`

**Interfaces:**
- Consumes: `encodeRoutineSet` (Task 5), `rleDecode`, `fromHex` (Task 4).
- Produces: a module exporting `createService(pebble, storage)` with `handleWebviewClosed(response)`, `handleMessage(dict)`, and `sendRoutines()`; `index.js` wires it to the real `Pebble` global.

Message contract (keys from `package.json`):

| Cmd | Direction | Fields |
|---|---|---|
| 1 `GET_ROUTINES` | watch → phone | — |
| 2 `ROUTINE_SET` | phone → watch | `ChunkIndex`, `ChunkCount`, `PayloadLen`, `Chunk` |
| 3 `GET_ART` | watch → phone | `RoutineId`, `StepIndex` |
| 4 `ART` | phone → watch | `RoutineId`, `StepIndex`, `ChunkIndex`, `ChunkCount`, `PayloadLen`, `Chunk` |

Art payload: 1344 raw bytes (96×112), chunked at 672 bytes → two messages.

- [ ] **Step 1: Write the failing tests**

With a fake `Pebble` recording `sendAppMessage` calls and an in-memory storage:
- `handleWebviewClosed(encodeURIComponent(JSON.stringify({routines:[...], art:[{r:0,s:0,data:"<rle hex>"}]})))` stores the routines and raw art, and sends `ROUTINE_SET` chunks whose concatenation decodes to one routine.
- A second `handleWebviewClosed` replaces the previous set (no stale routines).
- `handleMessage({Cmd:1})` re-sends the full set.
- `handleMessage({Cmd:3, RoutineId:0, StepIndex:0})` sends two `ART` messages whose chunks concatenate to the 1344-byte image.
- `handleMessage({Cmd:3, RoutineId:5, StepIndex:0})` for a missing picture sends nothing.
- Malformed close payload (`"not-json"`) leaves stored data unchanged.

- [ ] **Step 2: Run and watch it fail**

Run: `node --test test/`
Expected: module not found.

- [ ] **Step 3: Implement the service**

Storage layout: `intervals.routines` = JSON array; `intervals.art` = JSON map `"r:s" → Array(1344)` (plain array of numbers; no base64 needed since `localStorage` is stringified JSON). Send chunks sequentially with a callback chain so messages do not interleave.

- [ ] **Step 4: Run tests and confirm pass**

Run: `node --test test/`
Expected: all passing.

- [ ] **Step 5: Commit**

```bash
git add src/pkjs/index.js test/pkjs.test.js test/fake-pebble.js
git commit -m "feat: pebblekit js routine and art service"
```

---

### Task 7: Watch protocol, chunk reassembly, and bitmap building

**Files:**
- Create: `src/protocol.h`, `src/protocol.c`
- Modify: `src/main.c`

**Interfaces:**
- Consumes: `store_commit_blob`, `store_load_all` (Task 2).
- Produces (Pebble-only):
  - `void protocol_init(void);`
  - `void protocol_request_routines(void);`
  - `void protocol_request_art(uint8_t routine_id, uint8_t step_index);`
  - `GBitmap *protocol_take_art(uint8_t step_index);` — returns the last fully received image for that step, or `NULL`.
  - `GBitmap *protocol_thumb(uint8_t step_index);` — 48×56 nearest 2×2 majority downsample, built lazily.

- [ ] **Step 1: Add the receive path**

`app_message_open(app_message_inbox_size_maximum(), 256)`. Inbox handler dispatches on `Cmd`. `ROUTINE_SET` accumulates chunks into a 4 KB static buffer; when `ChunkCount` chunks are present, calls `store_commit_blob`. `ART` accumulates into a 1344-byte static buffer; on completion, allocates `gbitmap_create_blank(GSize(96,112), GBitmapFormat1Bit)` and copies 12-byte rows into `gbitmap_get_data()` using `gbitmap_get_bytes_per_row()`. Any gap, bad length, or index mismatch discards the partial buffer silently.

- [ ] **Step 2: Build and install**

Run: `pebble build && pebble install --phone <ip>`
Expected: build clean; app still runs.

- [ ] **Step 3: Log-based device check**

Temporarily log chunk arrivals and the committed routine count with `APP_LOG`. Send a test `ROUTINE_SET` from the phone by opening the config page stub (Task 9) or a `pebble send-app-message` equivalent. Expected: log shows the count, and relaunching the app retains it (persist works).

- [ ] **Step 4: Verify the bitmap path on device**

Log `gbitmap_get_bytes_per_row()` (expected 12) and draw the received bitmap into the placeholder window. Expected: the picture appears with no tearing or inverted black/white.

- [ ] **Step 5: Verify failure handling on device**

Send a `ROUTINE_SET` missing its last chunk, then relaunch. Expected: log shows the commit was rejected and the previous routine list is unchanged. Likewise, start a pictured routine and clear the chunk buffer mid-way (log the discard) → the picture is absent but the session runs.

- [ ] **Step 6: Commit**

```bash
git add src/protocol.h src/protocol.c src/main.c
git commit -m "feat: appmessage protocol and art reassembly"
```

---

### Task 8: Watch UI — menu, sync, session

**Files:**
- Create: `src/ui.h`, `src/ui.c`
- Modify: `src/main.c`

**Interfaces:**
- Consumes: `store_load_all` (Task 2), `Session` API (Task 3), `protocol_*` (Task 7).
- Produces:
  - `void ui_show_menu(void);`
  - `void ui_start_session(uint8_t routine_id);`
  - `void ui_sync_progress(int received, int total);`
  - `void ui_sync_failed(bool timed_out);`

- [ ] **Step 1: Menu window**

`MenuLayer` or `SimpleMenuLayer` over `store_load_all`. Select starts the routine; Back exits. Empty state: "No routines — configure in the Pebble app".

- [ ] **Step 2: Session window**

Three text layers plus a `BitmapLayer` (hidden when no picture). Work layout: phase label, step name, `MM:SS` in a large font, picture 96×112 below. Rest layout: phase label, "UP NEXT", next step name, countdown, 48×56 thumbnail. `app_timer_register` at 1000 ms only redraws; state comes from `session_update(time(NULL)*1000 + ...)`. Select → `session_toggle_pause`, Down → `session_skip`, Back → free the session and pop the window. Buzz in the tick where `session_update` reports a change; buzz triple when the phase becomes `PHASE_DONE`, then show "DONE" for 3 s and pop.

- [ ] **Step 3: Sync window**

Shown when the routine has any `has_art` step. Requests art for each such step, updates a progress layer ("Sending pictures 3/10"), and starts the session on the 8 s deadline or on Select, with whatever pictures arrived. No response within 3 s → replace progress with "No phone — start without pictures?" plus Select/Back.

- [ ] **Step 4: Build, install, and run a full routine on the watch**

Expected: menu lists routines; a text-only routine runs with correct countdowns, pause, skip, buzzes; a pictured routine shows art on work and thumbnail on rest; Back exits cleanly.

- [ ] **Step 5: Check the phone-absent path**

Turn the phone's Bluetooth off, start the pictured routine on the watch. Expected: after ~3 s the "start without pictures?" prompt appears; Select runs the routine text-only with correct timings.

- [ ] **Step 6: Commit**

```bash
git add src/ui.h src/ui.c src/main.c
git commit -m "feat: menu, session, and sync screens"
```

---

### Task 9: Config page builder

**Files:**
- Create: `src/pkjs/config/index.html`
- Create: `src/pkjs/config/app.js`
- Modify: `src/pkjs/index.js` (open the page in `showConfiguration`)

**Interfaces:**
- Consumes: `convert.js` helpers (Task 4), the message/blob contracts (Tasks 2, 5).
- Produces: on Save, `location = return_to + encodeURIComponent(JSON.stringify({routines, art}))`, where `art` is `[{r, s, data: "<rle hex>"}]`.

- [ ] **Step 1: Builder UI**

Routine list → editor with name and ordered steps (name, work s, rest s, picture). Buttons: add step, remove step, move up/down, add routine, delete routine, Save. Read the return URL from `?return_to=` with the `pebblejs://close#` default.

- [ ] **Step 2: Picture conversion**

`<input type="file" accept="image/*">` → `createImageBitmap` → draw into an offscreen canvas through `fitRect` → grayscale → `packThreshold` at cutoff 128 with a slider (64–192) → render the packed bytes back into a 96×112 canvas as the exact 1:1 preview (black/white). Show bytes used.

- [ ] **Step 3: Budgets**

Routine blob ≤ 3072 bytes (via `encodeRoutineSet`) and total art ≤ 20 KB (12 pictured steps). On violation, disable Save and show the reason ("Art budget exceeded: 13/12 pictures").

- [ ] **Step 4: Save + validate in a browser**

Serve locally (`python3 -m http.server -d src/pkjs/config 8000`), open in Chrome, add a routine with a photo, confirm the preview is legible, Save writes the close URL, and the payload's routines decode with `encodeRoutineSet`.

- [ ] **Step 5: Validate on the phone**

Host the page (GitHub Pages or any static host), open it from the Pebble app's gear icon, confirm `<input type=file>` opens the Android picker and the ~10 KB close URL is accepted.

- [ ] **Step 6: Commit**

```bash
git add src/pkjs/config/index.html src/pkjs/config/app.js src/pkjs/index.js
git commit -m "feat: phone routine builder with 1-bit picture preview"
```

---

### Task 10: README, hosting notes, end-to-end pass

**Files:**
- Create: `README.md`
- Modify: `src/pkjs/index.js` (config URL constant)

**Interfaces:**
- Consumes: everything.
- Produces: reproducible install instructions.

- [ ] **Step 1: Write `README.md`**

Sections: what it does, install (`pebble build`, `pebble install --phone <ip>`), hosting the config page (GitHub Pages steps, the `CONFIG_URL` constant), how to add routines, limits (8 routines, 15 steps, 12 pictures, ~20 KB art), known limitations (phone needed at start for pictures, Android only).

- [ ] **Step 2: End-to-end on hardware**

Create two routines (a 10-step pictured warmup, a text-only stretch), sync, run both, pause/skip/back, then re-open with the phone in airplane mode and confirm text-only start works.

- [ ] **Step 3: Update the spec's spike list with findings**

Mark each spike item resolved or, if it failed, record the workaround actually used.

- [ ] **Step 4: Commit**

```bash
git add README.md src/pkjs/index.js docs/superpowers/specs/
git commit -m "docs: install and hosting instructions"
```
