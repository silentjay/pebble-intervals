#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "session.h"

static Routine make_routine(void) {
  Routine r = {0};
  strcpy(r.name, "Test");
  r.step_count = 3;
  r.steps[0].work_s = 30; r.steps[0].rest_s = 10;
  r.steps[1].work_s = 45; r.steps[1].rest_s = 0;
  r.steps[2].work_s = 20; r.steps[2].rest_s = 5;
  return r;
}

static void test_sequence(void) {
  Routine r = make_routine();
  Session s;
  session_start(&s, &r, 0);
  assert(s.phase == PHASE_WORK && s.step == 0);
  assert(session_remaining_ms(&s, 0) == 30000);

  assert(session_update(&s, 30000));
  assert(s.phase == PHASE_REST && s.step == 0);

  assert(session_update(&s, 40000));
  assert(s.phase == PHASE_WORK && s.step == 1);

  assert(session_update(&s, 85000));
  assert(s.phase == PHASE_WORK && s.step == 2);

  assert(session_update(&s, 105000));
  assert(s.phase == PHASE_DONE);
  assert(session_remaining_ms(&s, 105000) == 0);
  puts("sequence: ok");
}

static void test_pause_resume(void) {
  Routine r = make_routine();
  Session s;
  session_start(&s, &r, 0);
  session_toggle_pause(&s, 5000);
  assert(s.paused);
  assert(session_remaining_ms(&s, 60000) == 25000);
  assert(!session_update(&s, 90000));
  assert(s.phase == PHASE_WORK && s.step == 0);

  session_toggle_pause(&s, 60000);
  assert(!s.paused);
  assert(session_remaining_ms(&s, 60000) == 25000);
  assert(session_update(&s, 85000));
  assert(s.phase == PHASE_REST && s.step == 0);
  puts("pause/resume: ok");
}

static void test_skip(void) {
  Routine r = make_routine();
  Session s;
  session_start(&s, &r, 0);

  session_skip(&s, 1000);
  assert(s.phase == PHASE_REST && s.step == 0);

  session_skip(&s, 2000);
  assert(s.phase == PHASE_WORK && s.step == 1);

  session_skip(&s, 3000);
  assert(s.phase == PHASE_WORK && s.step == 2);

  session_skip(&s, 4000);
  assert(s.phase == PHASE_DONE);
  puts("skip: ok");
}

static void test_zero_rest_and_single_step(void) {
  Routine r = {0};
  r.step_count = 1;
  r.steps[0].work_s = 10;
  r.steps[0].rest_s = 10;
  Session s;
  session_start(&s, &r, 0);
  assert(session_update(&s, 10000));
  assert(s.phase == PHASE_DONE);
  puts("trailing rest skipped: ok");
}

static void test_empty_routine(void) {
  Routine r = {0};
  Session s;
  session_start(&s, &r, 0);
  assert(s.phase == PHASE_DONE);
  assert(!session_update(&s, 5000));
  puts("empty routine: ok");
}

int main(void) {
  test_sequence();
  test_pause_resume();
  test_skip();
  test_zero_rest_and_single_step();
  test_empty_routine();
  puts("session: ok");
  return 0;
}
