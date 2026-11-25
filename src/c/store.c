#include "store.h"

#include <string.h>

#ifdef PEBBLE
#include <pebble.h>
#endif

static bool put_u8(uint8_t *out, size_t cap, size_t *n, uint8_t v) {
  if (*n >= cap) return false;
  out[(*n)++] = v;
  return true;
}

static bool put_u16(uint8_t *out, size_t cap, size_t *n, uint16_t v) {
  return put_u8(out, cap, n, v & 0xFF) && put_u8(out, cap, n, v >> 8);
}

static bool put_name(uint8_t *out, size_t cap, size_t *n, const char *name) {
  size_t len = strlen(name);
  if (len > NAME_MAX) len = NAME_MAX;
  if (!put_u8(out, cap, n, (uint8_t)len)) return false;
  if (*n + len > cap) return false;
  memcpy(out + *n, name, len);
  *n += len;
  return true;
}

int store_encode_blob(const Routine *in, int count, uint8_t *out, size_t cap) {
  if (count < 0 || count > MAX_ROUTINES) return -1;

  size_t n = 0;
  if (!put_u8(out, cap, &n, (uint8_t)count)) return -1;

  for (int i = 0; i < count; i++) {
    const Routine *r = &in[i];
    uint8_t steps = r->step_count < MAX_STEPS ? r->step_count : MAX_STEPS;
    if (!put_u8(out, cap, &n, steps)) return -1;
    if (!put_name(out, cap, &n, r->name)) return -1;
    for (int s = 0; s < steps; s++) {
      const Step *step = &r->steps[s];
      if (!put_name(out, cap, &n, step->name)) return -1;
      if (!put_u16(out, cap, &n, step->work_s)) return -1;
      if (!put_u16(out, cap, &n, step->rest_s)) return -1;
      if (!put_u8(out, cap, &n, step->flags)) return -1;
    }
  }
  return (int)n;
}

static bool get_u8(const uint8_t *in, size_t len, size_t *p, uint8_t *v) {
  if (*p >= len) return false;
  *v = in[(*p)++];
  return true;
}

static bool get_u16(const uint8_t *in, size_t len, size_t *p, uint16_t *v) {
  uint8_t lo, hi;
  if (!get_u8(in, len, p, &lo) || !get_u8(in, len, p, &hi)) return false;
  *v = (uint16_t)(lo | (hi << 8));
  return true;
}

static bool get_name(const uint8_t *in, size_t len, size_t *p, char *dst) {
  uint8_t n;
  if (!get_u8(in, len, p, &n) || n > NAME_MAX || *p + n > len) return false;
  memcpy(dst, in + *p, n);
  dst[n] = '\0';
  *p += n;
  return true;
}

int store_decode_blob(const uint8_t *in, size_t len, Routine *out, int max) {
  Routine tmp[MAX_ROUTINES];
  size_t p = 0;
  uint8_t count;

  if (max > MAX_ROUTINES) max = MAX_ROUTINES;
  if (!get_u8(in, len, &p, &count) || count > max) return -1;

  for (int i = 0; i < count; i++) {
    Routine *r = &tmp[i];
    memset(r, 0, sizeof *r);
    if (!get_u8(in, len, &p, &r->step_count) || r->step_count > MAX_STEPS) return -1;
    if (!get_name(in, len, &p, r->name)) return -1;
    for (int s = 0; s < r->step_count; s++) {
      Step *step = &r->steps[s];
      if (!get_name(in, len, &p, step->name)) return -1;
      if (!get_u16(in, len, &p, &step->work_s)) return -1;
      if (!get_u16(in, len, &p, &step->rest_s)) return -1;
      if (!get_u8(in, len, &p, &step->flags)) return -1;
    }
  }

  memcpy(out, tmp, sizeof(tmp[0]) * count);
  return count;
}

#ifdef PEBBLE

#define STORE_KEY_COUNT 0x0001
#define STORE_KEY_HEADER(rid) (0x0800 + (rid))
#define STORE_KEY_STEP(rid, i) (0x1000 + (rid) * MAX_STEPS + (i))

typedef struct {
  uint8_t step_count;
  char name[NAME_MAX + 1];
} HeaderRecord;

typedef struct {
  char name[NAME_MAX + 1];
  uint16_t work_s;
  uint16_t rest_s;
  uint8_t flags;
} StepRecord;

static Routine s_tmp[MAX_ROUTINES];

static void prv_delete_routine(int id) {
  persist_delete(STORE_KEY_HEADER(id));
  for (int i = 0; i < MAX_STEPS; i++) persist_delete(STORE_KEY_STEP(id, i));
}

bool store_commit_blob(const uint8_t *blob, size_t len) {
  int n = store_decode_blob(blob, len, s_tmp, MAX_ROUTINES);
  if (n < 0) return false;

  int old = persist_exists(STORE_KEY_COUNT) ? persist_read_int(STORE_KEY_COUNT) : 0;
  for (int i = n; i < old; i++) prv_delete_routine(i);

  persist_write_int(STORE_KEY_COUNT, n);
  for (int i = 0; i < n; i++) {
    const Routine *r = &s_tmp[i];
    HeaderRecord h = {0};
    h.step_count = r->step_count;
    strncpy(h.name, r->name, NAME_MAX);
    persist_write_data(STORE_KEY_HEADER(i), &h, sizeof h);

    for (int s = 0; s < r->step_count; s++) {
      StepRecord sr = {0};
      strncpy(sr.name, r->steps[s].name, NAME_MAX);
      sr.work_s = r->steps[s].work_s;
      sr.rest_s = r->steps[s].rest_s;
      sr.flags = r->steps[s].flags;
      persist_write_data(STORE_KEY_STEP(i, s), &sr, sizeof sr);
    }
  }
  return true;
}

int store_load_all(Routine *out, int max) {
  int n = persist_exists(STORE_KEY_COUNT) ? persist_read_int(STORE_KEY_COUNT) : 0;
  if (n > max) n = max;
  if (n > MAX_ROUTINES) n = MAX_ROUTINES;

  for (int i = 0; i < n; i++) {
    Routine *r = &out[i];
    memset(r, 0, sizeof *r);
    HeaderRecord h = {0};
    if (persist_read_data(STORE_KEY_HEADER(i), &h, sizeof h) != (int)sizeof h) return i;
    h.name[NAME_MAX] = '\0';
    strcpy(r->name, h.name);
    r->step_count = h.step_count > MAX_STEPS ? MAX_STEPS : h.step_count;

    for (int s = 0; s < r->step_count; s++) {
      StepRecord sr = {0};
      if (persist_read_data(STORE_KEY_STEP(i, s), &sr, sizeof sr) != (int)sizeof sr) {
        r->step_count = s;
        break;
      }
      sr.name[NAME_MAX] = '\0';
      strcpy(r->steps[s].name, sr.name);
      r->steps[s].work_s = sr.work_s;
      r->steps[s].rest_s = sr.rest_s;
      r->steps[s].flags = sr.flags;
    }
  }
  return n;
}

#endif
