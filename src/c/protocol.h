#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pebble.h>

#define ART_W 96
#define ART_H 112
#define ART_STRIDE 12
#define ART_BYTES (ART_STRIDE * ART_H)

void protocol_init(void);
void protocol_request_routines(void);
void protocol_request_art(uint8_t routine_id, uint8_t step_index);
GBitmap *protocol_art(uint8_t step_index);
GBitmap *protocol_thumb(uint8_t step_index);
void protocol_clear_art(void);
