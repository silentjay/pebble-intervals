#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "store.h"

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
// Returns true when the phase or step changed (caller buzzes).
bool session_update(Session *s, uint32_t now_ms);
void session_toggle_pause(Session *s, uint32_t now_ms);
void session_skip(Session *s, uint32_t now_ms);
uint32_t session_remaining_ms(const Session *s, uint32_t now_ms);
