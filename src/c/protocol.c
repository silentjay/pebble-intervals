#include "protocol.h"

#include <string.h>

#include "store.h"

#define MAX_BLOB 3072
#define MAX_CHUNKS 32

static uint8_t s_blob[MAX_BLOB];
static uint16_t s_blob_len;
static uint16_t s_blob_expected;
static uint8_t s_blob_count;
static uint32_t s_blob_seen;

static uint8_t s_art_buf[ART_BYTES];
static uint8_t s_art_step;
static uint8_t s_art_routine;
static bool s_art_active;
static uint8_t s_art_count;
static uint32_t s_art_seen;

static GBitmap *s_art[MAX_STEPS];
static GBitmap *s_thumb[MAX_STEPS];
static void (*s_commit_cb)(void);

static void prv_send(uint8_t cmd, int routine_id, int step_index) {
  DictionaryIterator *out;
  if (app_message_outbox_begin(&out) != APP_MSG_OK) return;
  dict_write_uint8(out, MESSAGE_KEY_Cmd, cmd);
  if (routine_id >= 0) dict_write_uint8(out, MESSAGE_KEY_RoutineId, (uint8_t)routine_id);
  if (step_index >= 0) dict_write_uint8(out, MESSAGE_KEY_StepIndex, (uint8_t)step_index);
  app_message_outbox_send();
}

void protocol_request_routines(void) {
  s_blob_len = 0;
  s_blob_count = 0;
  s_blob_seen = 0;
  prv_send(1, -1, -1);
}

void protocol_request_art(uint8_t routine_id, uint8_t step_index) {
  s_art_active = true;
  s_art_routine = routine_id;
  s_art_step = step_index;
  s_art_count = 0;
  s_art_seen = 0;
  prv_send(3, routine_id, step_index);
}

void protocol_clear_art(void) {
  for (int i = 0; i < MAX_STEPS; i++) {
    if (s_art[i]) {
      gbitmap_destroy(s_art[i]);
      s_art[i] = NULL;
    }
    if (s_thumb[i]) {
      gbitmap_destroy(s_thumb[i]);
      s_thumb[i] = NULL;
    }
  }
  s_art_active = false;
}

GBitmap *protocol_art(uint8_t step_index) {
  return step_index < MAX_STEPS ? s_art[step_index] : NULL;
}

// 2x2 majority downsample: 72x84 -> 36x42.
GBitmap *protocol_thumb(uint8_t step_index) {
  if (step_index >= MAX_STEPS || !s_art[step_index]) return NULL;
  if (s_thumb[step_index]) return s_thumb[step_index];

  GBitmap *src = s_art[step_index];
  GBitmap *dst = gbitmap_create_blank(GSize(ART_W / 2, ART_H / 2), GBitmapFormat1Bit);
  if (!dst) return NULL;

  const uint8_t *src_data = gbitmap_get_data(src);
  uint8_t *dst_data = gbitmap_get_data(dst);
  int src_stride = gbitmap_get_bytes_per_row(src);
  int dst_stride = gbitmap_get_bytes_per_row(dst);

  for (int y = 0; y < ART_H / 2; y++) {
    for (int x = 0; x < ART_W / 2; x++) {
      int lit = 0;
      for (int dy = 0; dy < 2; dy++) {
        for (int dx = 0; dx < 2; dx++) {
          int sx = x * 2 + dx, sy = y * 2 + dy;
          if (src_data[sy * src_stride + (sx >> 3)] & (0x80 >> (sx & 7))) lit++;
        }
      }
      if (lit >= 2) dst_data[y * dst_stride + (x >> 3)] |= 0x80 >> (x & 7);
    }
  }
  s_thumb[step_index] = dst;
  return dst;
}

static void prv_finish_art(void) {
  if (s_art_step >= MAX_STEPS) return;
  if (s_art[s_art_step]) return;  // keep the first complete image

  GBitmap *bmp = gbitmap_create_blank(GSize(ART_W, ART_H), GBitmapFormat1Bit);
  if (!bmp) {
    APP_LOG(APP_LOG_LEVEL_ERROR, "art: no memory for %d", s_art_step);
    return;
  }

  int stride = gbitmap_get_bytes_per_row(bmp);
  uint8_t *dst = gbitmap_get_data(bmp);
  for (int y = 0; y < ART_H; y++) {
    memcpy(dst + y * stride, s_art_buf + y * ART_STRIDE, ART_STRIDE);
  }
  s_art[s_art_step] = bmp;
  APP_LOG(APP_LOG_LEVEL_INFO, "art ready: routine %d step %d", s_art_routine, s_art_step);
}

static void prv_inbox(DictionaryIterator *iter, void *context) {
  Tuple *cmd_t = dict_find(iter, MESSAGE_KEY_Cmd);
  if (!cmd_t) return;

  if (cmd_t->value->uint8 == 2) {
    Tuple *index_t = dict_find(iter, MESSAGE_KEY_ChunkIndex);
    Tuple *count_t = dict_find(iter, MESSAGE_KEY_ChunkCount);
    Tuple *len_t = dict_find(iter, MESSAGE_KEY_PayloadLen);
    Tuple *chunk_t = dict_find(iter, MESSAGE_KEY_Chunk);
    if (!index_t || !count_t || !len_t || !chunk_t) return;
    if (count_t->value->uint8 == 0 || count_t->value->uint8 > MAX_CHUNKS) return;
    if (len_t->value->uint16 > MAX_BLOB) return;

    if (index_t->value->uint8 == 0) {
      s_blob_count = count_t->value->uint8;
      s_blob_expected = len_t->value->uint16;
      s_blob_seen = 0;
      s_blob_len = 0;
    }
    if (count_t->value->uint8 != s_blob_count || index_t->value->uint8 >= s_blob_count) return;

    size_t offset = index_t->value->uint8 * 128;  // ROUTINE_CHUNK in index.js
    if (offset + chunk_t->length > MAX_BLOB) return;
    memcpy(s_blob + offset, chunk_t->value->data, chunk_t->length);
    if (offset + chunk_t->length > s_blob_len) s_blob_len = offset + chunk_t->length;
    s_blob_seen |= 1u << index_t->value->uint8;

    uint32_t all = (s_blob_count == 32) ? 0xFFFFFFFFu : ((1u << s_blob_count) - 1);
    if ((s_blob_seen & all) == all && s_blob_len == s_blob_expected) {
      if (store_commit_blob(s_blob, s_blob_len)) {
        APP_LOG(APP_LOG_LEVEL_INFO, "routines: committed %d bytes", s_blob_len);
        if (s_commit_cb) s_commit_cb();
      }
      s_blob_seen = 0;
      s_blob_len = 0;
    }
  } else if (cmd_t->value->uint8 == 4) {
    Tuple *rid_t = dict_find(iter, MESSAGE_KEY_RoutineId);
    Tuple *step_t = dict_find(iter, MESSAGE_KEY_StepIndex);
    Tuple *index_t = dict_find(iter, MESSAGE_KEY_ChunkIndex);
    Tuple *count_t = dict_find(iter, MESSAGE_KEY_ChunkCount);
    Tuple *chunk_t = dict_find(iter, MESSAGE_KEY_Chunk);
    if (!rid_t || !step_t || !index_t || !count_t || !chunk_t) return;
    if (count_t->value->uint8 == 0) return;
    if (!s_art_active || rid_t->value->uint8 != s_art_routine || step_t->value->uint8 != s_art_step) return;

    if (index_t->value->uint8 == 0) {
      s_art_count = count_t->value->uint8;
      s_art_seen = 0;
      memset(s_art_buf, 0, sizeof s_art_buf);
    }
    if (count_t->value->uint8 != s_art_count || index_t->value->uint8 >= s_art_count) return;

    size_t offset = index_t->value->uint8 * ART_CHUNK_BYTES;
    if (offset + chunk_t->length > ART_BYTES) return;
    memcpy(s_art_buf + offset, chunk_t->value->data, chunk_t->length);
    s_art_seen |= 1u << index_t->value->uint8;

    uint32_t all = (s_art_count == 32) ? 0xFFFFFFFFu : ((1u << s_art_count) - 1);
    if ((s_art_seen & all) == all) {
      prv_finish_art();
      s_art_active = false;
    }
  }
}

void protocol_set_commit_callback(void (*cb)(void)) {
  s_commit_cb = cb;
}

static void prv_inbox_dropped(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_WARNING, "appmessage dropped: %d", (int)reason);
}

void protocol_init(void) {
  s_blob_len = 0;
  s_blob_count = 0;
  s_blob_seen = 0;
  s_art_active = false;
  app_message_register_inbox_received(prv_inbox);
  app_message_register_inbox_dropped(prv_inbox_dropped);
  app_message_open(app_message_inbox_size_maximum(), 256);
}
