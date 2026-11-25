#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_ROUTINES 8
#define MAX_STEPS 15
#define NAME_MAX 20

#define STEP_HAS_ART 0x01

typedef struct {
  char name[NAME_MAX + 1];
  uint16_t work_s;
  uint16_t rest_s;
  uint8_t flags;
} Step;

typedef struct {
  char name[NAME_MAX + 1];
  uint8_t step_count;
  Step steps[MAX_STEPS];
} Routine;

int store_encode_blob(const Routine *in, int count, uint8_t *out, size_t cap);
int store_decode_blob(const uint8_t *in, size_t len, Routine *out, int max);

#ifdef PEBBLE
bool store_commit_blob(const uint8_t *blob, size_t len);
int store_load_all(Routine *out, int max);
#endif
