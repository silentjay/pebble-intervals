#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "store.h"

static const uint8_t FIXTURE[] = {
  0x01, 0x02, 0x04, 'W', 'a', 'r', 'm',
  0x03, 'A', 'r', 'm', 0x1E, 0x00, 0x0A, 0x00, 0x00,
  0x03, 'L', 'e', 'g', 0x2D, 0x00, 0x00, 0x00, 0x01,
};

static void test_encode_fixture(void) {
  Routine r = {0};
  strcpy(r.name, "Warm");
  r.step_count = 2;
  strcpy(r.steps[0].name, "Arm");
  r.steps[0].work_s = 30;
  r.steps[0].rest_s = 10;
  strcpy(r.steps[1].name, "Leg");
  r.steps[1].work_s = 45;
  r.steps[1].rest_s = 0;
  r.steps[1].flags = STEP_HAS_ART;

  uint8_t out[256];
  int n = store_encode_blob(&r, 1, out, sizeof out);
  assert(n == (int)sizeof FIXTURE);
  assert(memcmp(out, FIXTURE, sizeof FIXTURE) == 0);
  puts("encode fixture: ok");
}

static void test_decode_fixture(void) {
  Routine out[2];
  memset(out, 0xEE, sizeof out);
  int n = store_decode_blob(FIXTURE, sizeof FIXTURE, out, 2);
  assert(n == 1);
  assert(strcmp(out[0].name, "Warm") == 0);
  assert(out[0].step_count == 2);
  assert(strcmp(out[0].steps[0].name, "Arm") == 0);
  assert(out[0].steps[0].work_s == 30);
  assert(out[0].steps[0].rest_s == 10);
  assert(out[0].steps[0].flags == 0);
  assert(strcmp(out[0].steps[1].name, "Leg") == 0);
  assert(out[0].steps[1].work_s == 45);
  assert(out[0].steps[1].rest_s == 0);
  assert(out[0].steps[1].flags == STEP_HAS_ART);
  puts("decode fixture: ok");
}

static void test_long_name_truncated(void) {
  Routine r = {0};
  memset(r.name, 'A', sizeof r.name - 1);
  r.step_count = 1;
  memset(r.steps[0].name, 'B', sizeof r.steps[0].name - 1);
  r.steps[0].work_s = 1;

  uint8_t out[256];
  int n = store_encode_blob(&r, 1, out, sizeof out);
  assert(n > 0);
  assert(out[2] == NAME_MAX);
  assert(out[2 + 1 + NAME_MAX] == NAME_MAX);
  puts("long name truncation: ok");
}

static void test_truncated_blob_rejected(void) {
  Routine out[2];
  memset(out, 0x5A, sizeof out);
  Routine before = out[0];
  assert(store_decode_blob(FIXTURE, sizeof FIXTURE - 1, out, 2) == -1);
  assert(memcmp(&before, &out[0], sizeof before) == 0);
  puts("truncated blob rejected: ok");
}

static void test_too_many_routines(void) {
  Routine r = {0};
  r.step_count = 1;
  uint8_t out[4096];
  assert(store_encode_blob(&r, MAX_ROUTINES + 1, out, sizeof out) == -1);
  puts("max routines: ok");
}

int main(void) {
  test_encode_fixture();
  test_decode_fixture();
  test_long_name_truncated();
  test_truncated_blob_rejected();
  test_too_many_routines();
  puts("store: ok");
  return 0;
}
