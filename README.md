# Pebble Intervals

An interval timer for the Pebble 2 (Pebble 2 HR / SE, `diorite`). Routines are
built on the phone; each step can carry a 1-bit picture of the movement.

- Menu of routines on the watch.
- Work period: step name, countdown, picture.
- Rest period: next step, countdown, smaller picture.
- Select pauses, Down skips to the next period, Back ends the session.
- Buzz on every transition, triple buzz when done.
- Trailing rest is skipped: the routine ends with the last work period.

## Install

Requires the Pebble SDK (`pebble` CLI) and a paired watch with the developer
connection enabled.

```bash
pebble build
pebble install --phone <watch-or-phone-ip>
```

The app is `build/pebble-intervals.pbw`, also installable from the phone via
`pebble install` while the phone is on the same network.

## Configure routines

The config page is static HTML and must be hosted at a URL, because PebbleKit JS
only runs inside the Pebble app. Host `src/pkjs/` (the page is
`config/index.html`, and it loads `../convert.js`).

1. Put the contents of `src/pkjs/` on any static host (GitHub Pages works:
   push the directory as the site root).
2. Set `CONFIG_URL` at the top of `src/pkjs/index.js` to the page URL, e.g.
   `https://you.github.io/pebble-intervals/config/index.html`.
3. Rebuild and reinstall.
4. In the Pebble app, open the app's settings (gear icon) to edit routines.
5. Save: the payload returns to the phone, the routines sync to the watch, and
   the pictures are kept on the phone.

## Limits

- 8 routines, 15 steps each, 20-byte names.
- 12 pictured steps per routine; pictures are 72×84, black and white.
- Routine definitions use the watch's 4 KB of persistent storage (≤ 3 KB used).
- Pictures are streamed from the phone when a session starts and live in the
  watch's RAM for that session only. **The phone must be in Bluetooth range at
  the start of a pictured routine.** Without it, the routine runs text-only.
- Android only (the config page uses the Android photo picker).

## Tests

```bash
test/run_c_tests.sh   # store encoding and session state machine (host)
node --test test/     # 1-bit packing, RLE, protocol service, builder payloads
```

## How it fits together

- `src/c/store.c` — compact routine encoding and watch persistence.
- `src/c/session.c` — work/rest/pause/skip state machine (host-tested).
- `src/c/protocol.c` — AppMessage chunking, routine commits, picture reassembly.
- `src/c/ui.c` — menu, sync and session screens.
- `src/pkjs/index.js` — phone-side store and server for routines/pictures.
- `src/pkjs/convert.js` — 1-bit packing, RLE, routine blob encoding.
- `src/pkjs/config/` — the builder page.
