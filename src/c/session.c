#include "session.h"

#include <string.h>

static uint32_t prv_seconds(const Session *s) {
  if (s->phase == PHASE_WORK) return s->routine->steps[s->step].work_s;
  if (s->phase == PHASE_REST) return s->routine->steps[s->step].rest_s;
  return 0;
}

// Ends the current period early and enters the next one, starting at `now`.
// A work period is followed by its rest only if that rest is non-zero; the
// last work period goes straight to DONE, skipping its trailing rest.
static void prv_next(Session *s, uint32_t now) {
  if (s->phase == PHASE_WORK) {
    if (s->step + 1 >= s->routine->step_count) {
      s->phase = PHASE_DONE;
      return;
    }
    if (s->routine->steps[s->step].rest_s > 0) {
      s->phase = PHASE_REST;
      s->end_ms = now + s->routine->steps[s->step].rest_s * 1000;
    } else {
      s->step++;
      s->phase = PHASE_WORK;
      s->end_ms = now + s->routine->steps[s->step].work_s * 1000;
    }
  } else if (s->phase == PHASE_REST) {
    if (s->step + 1 >= s->routine->step_count) {
      s->phase = PHASE_DONE;
      return;
    }
    s->step++;
    s->phase = PHASE_WORK;
    s->end_ms = now + s->routine->steps[s->step].work_s * 1000;
  }
}

void session_start(Session *s, const Routine *r, uint32_t now_ms) {
  memset(s, 0, sizeof *s);
  s->routine = r;
  if (r->step_count == 0) {
    s->phase = PHASE_DONE;
    return;
  }
  s->phase = PHASE_WORK;
  s->end_ms = now_ms + prv_seconds(s) * 1000;
  session_update(s, now_ms);  // consume zero-length opening periods
}

bool session_update(Session *s, uint32_t now_ms) {
  if (s->paused || s->phase == PHASE_DONE) return false;

  bool changed = false;
  int guard = 2 * MAX_STEPS + 4;
  while (s->phase != PHASE_DONE && (int32_t)(now_ms - s->end_ms) >= 0 && guard-- > 0) {
    prv_next(s, s->end_ms);
    changed = true;
  }
  return changed;
}

void session_toggle_pause(Session *s, uint32_t now_ms) {
  if (s->phase == PHASE_DONE) return;
  if (!s->paused) {
    s->paused_remaining_ms = (int32_t)(s->end_ms - now_ms) > 0 ? s->end_ms - now_ms : 0;
    s->paused = true;
  } else {
    s->end_ms = now_ms + s->paused_remaining_ms;
    s->paused = false;
  }
}

void session_skip(Session *s, uint32_t now_ms) {
  if (s->phase == PHASE_DONE) return;
  s->paused = false;
  prv_next(s, now_ms);
}

uint32_t session_remaining_ms(const Session *s, uint32_t now_ms) {
  if (s->phase == PHASE_DONE) return 0;
  if (s->paused) return s->paused_remaining_ms;
  int32_t left = (int32_t)(s->end_ms - now_ms);
  return left > 0 ? (uint32_t)left : 0;
}
