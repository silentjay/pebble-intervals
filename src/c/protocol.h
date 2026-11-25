#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <pebble.h>

#define ART_W 72
#define ART_H 84
#define ART_STRIDE 9
#define ART_BYTES (ART_STRIDE * ART_H)
#define ART_CHUNK_BYTES 378

void protocol_init(void);
void protocol_set_commit_callback(void (*cb)(void));
void protocol_request_routines(void);
void protocol_request_art(uint8_t routine_id, uint8_t step_index);
GBitmap *protocol_art(uint8_t step_index);
GBitmap *protocol_thumb(uint8_t step_index);
void protocol_clear_art(void);
