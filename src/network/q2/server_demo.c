/* Port of src/network/q2/server-demo.ts. GPL-2.0-or-later. */
#include "qa/network/q2/server_demo.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codec.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/demo.h"
#include "qa/network/q2/message.h"
#include "qa/foundation/js.h"

static char *dup_str(const char *s) {
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s, n + 1);
  return out;
}

static void state_free(qa_q2_server_demo_state *state) {
  free(state->gamedir);
  if (state->config_values != NULL) {
    for (size_t i = 0; i < state->config_count; i++) free(state->config_values[i]);
    free(state->config_values);
  }
  free(state->config_strings);
  memset(state, 0, sizeof *state);
}

static void record_dispose(qa_q2_server_demo_record *record) {
  state_free(&record->state);
  free(record->entities);
  record->entities = NULL;
  qa_q2_server_records_free(record->multicasts, record->multicast_count);
  record->multicasts = NULL;
  record->multicast_count = 0;
}

void qa_q2_server_demo_records_free(qa_q2_server_demo_record *records, size_t count) {
  if (records != NULL) {
    for (size_t i = 0; i < count; i++) record_dispose(&records[i]);
    free(records);
  }
}

static int config_compare(const void *a, const void *b) {
  double x = ((const qa_q2_server_demo_config *)a)->index;
  double y = ((const qa_q2_server_demo_config *)b)->index;
  return x < y ? -1 : x > y ? 1 : 0;
}

static const char *config_get(const qa_q2_server_demo_state *state, double index) {
  for (size_t i = 0; i < state->config_count; i++) {
    if (state->config_strings[i].index == index) return state->config_strings[i].value;
  }
  return NULL;
}

typedef struct signon_scope {
  qa_q2_sizebuf message;
  qa_q2_server_demo_config *sorted;
} signon_scope;
static void signon_release(void *arg) {
  signon_scope *scope = (signon_scope *)arg;
  free(scope->sorted);
  scope->sorted = NULL;
  qa_q2_message_destroy(&scope->message);
}

uint8_t *qa_q2_encode_server_demo_signon(const qa_q2_server_demo_state *state,
                                         size_t *out_length) {
  signon_scope scope;
  memset(&scope, 0, sizeof scope);
  if (!qa_q2_message_create(&scope.message, 32768)) return NULL;
  size_t count = state->config_count;
  scope.sorted = (qa_q2_server_demo_config *)malloc(count > 0 ? count * sizeof *scope.sorted
                                                              : sizeof *scope.sorted);
  if (scope.sorted == NULL) {
    qa_q2_message_destroy(&scope.message);
    return NULL;
  }
  /* SZ_GetSpace overflow and the range check raise mid-encode. */
  qa_cleanup node;
  qa_cleanup_push(&node, signon_release, &scope);
  qa_q2_sizebuf *message = &scope.message;
  qa_q2_msg_write_byte(message, 12);
  qa_q2_msg_write_long(message, 34);
  qa_q2_msg_write_long(message, state->servercount);
  qa_q2_msg_write_byte(message, 2);
  qa_q2_msg_write_string(message, state->gamedir);
  qa_q2_msg_write_short(message, -1);
  const char *level = config_get(state, 0);
  qa_q2_msg_write_string(message, level != NULL ? level : "");
  if (count > 0) memcpy(scope.sorted, state->config_strings, count * sizeof *scope.sorted);
  qa_js_sort(scope.sorted, count, sizeof *scope.sorted, config_compare); /* stable, as Array.sort */
  for (size_t i = 0; i < count; i++) {
    double index = scope.sorted[i].index;
    if (!isfinite(index) || index != trunc(index) || index < 0 || index >= 2080) {
      qa_range_error("Classic server demo configstring outside range");
    }
    if (scope.sorted[i].value == NULL || scope.sorted[i].value[0] == '\0') continue;
    qa_q2_msg_write_byte(message, 13);
    qa_q2_msg_write_short(message, index);
    qa_q2_msg_write_string(message, scope.sorted[i].value);
  }
  qa_cleanup_pop(&node);
  uint8_t *out = qa_q2_message_bytes(message, out_length);
  signon_release(&scope);
  return out;
}

typedef struct frame_scope {
  qa_q2_wire_codec wire;
  bool has_wire;
  qa_q2_sizebuf message;
} frame_scope;
static void frame_release(void *arg) {
  frame_scope *scope = (frame_scope *)arg;
  qa_q2_message_destroy(&scope->message);
  if (scope->has_wire) qa_q2_wire_codec_destroy(&scope->wire);
  scope->has_wire = false;
}
/* TS truthiness of a number field: 0 and NaN are false. */
static bool truthy(double value) { return value != 0 && !isnan(value); }

uint8_t *qa_q2_encode_server_demo_frame(double server_frame,
                                        const qa_q2_wire_entity *entities,
                                        size_t entity_count,
                                        const qa_q2_byte_span *multicasts,
                                        size_t multicast_count, size_t *out_length) {
  if (!isfinite(server_frame) || server_frame != trunc(server_frame) || server_frame < 0 ||
      server_frame > 0x7fffffff) {
    qa_range_error("Invalid server demo frame");
  }
  qa_q2_protocol_identity classic;
  memset(&classic, 0, sizeof classic);
  classic.kind = "q2-classic";
  classic.version = 34;
  frame_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_q2_sizebuf_init_empty(&scope.message);
  if (!qa_q2_wire_codec_init(&scope.wire, classic)) return NULL;
  scope.has_wire = true;
  if (!qa_q2_message_create(&scope.message, 32768)) {
    frame_release(&scope);
    return NULL;
  }
  qa_cleanup node;
  qa_cleanup_push(&node, frame_release, &scope);
  qa_q2_sizebuf *message = &scope.message;
  qa_q2_protocol_codec *codec = scope.wire.selected_codec;
  qa_q2_wire_entity zero;
  qa_q2_wire_entity_init(&zero);
  qa_q2_msg_write_byte(message, 20);
  qa_q2_msg_write_long(message, server_frame);
  codec->write_packet_entities_begin(codec, message);
  double previous = 0;
  for (size_t i = 0; i < entity_count; i++) {
    const qa_q2_wire_entity *entity = &entities[i];
    if (entity->number <= previous || entity->number >= 1024) {
      qa_range_error("Classic server demo entities must be ordered and unique");
    }
    previous = entity->number;
    if (truthy(entity->modelindex) || truthy(entity->effects) || truthy(entity->sound) ||
        truthy(entity->event)) {
      codec->write_delta_entity(codec, message, &zero, entity, false, true);
    }
  }
  codec->write_packet_entities_end(codec, message);
  for (size_t i = 0; i < multicast_count; i++) {
    qa_q2_sz_write(message, multicasts[i].bytes, multicasts[i].length,
                   (int64_t)multicasts[i].length);
  }
  qa_cleanup_pop(&node);
  uint8_t *out = qa_q2_message_bytes(message, out_length);
  frame_release(&scope);
  return out;
}

/* Everything readQ2ServerDemo holds while it decodes; a raise anywhere releases it. */
typedef struct demo_read_scope {
  qa_q2_wire_codec wire;
  bool has_wire;
  qa_q2_server_message_reader multicast;
  bool has_multicast;
  qa_q2_demo_record *demo_records;
  size_t demo_count;
  qa_q2_server_demo_record *results;
  size_t count, capacity;
  qa_q2_server_demo_record record; /* the record being decoded */
} demo_read_scope;
static void demo_read_release(void *arg) {
  demo_read_scope *scope = (demo_read_scope *)arg;
  record_dispose(&scope->record);
  qa_q2_server_demo_records_free(scope->results, scope->count);
  scope->results = NULL;
  scope->count = 0;
  qa_q2_demo_records_free(scope->demo_records, scope->demo_count);
  scope->demo_records = NULL;
  scope->demo_count = 0;
  if (scope->has_multicast) qa_q2_server_message_reader_destroy(&scope->multicast);
  scope->has_multicast = false;
  if (scope->has_wire) qa_q2_wire_codec_destroy(&scope->wire);
  scope->has_wire = false;
}

/* Appends one configStrings entry (TS Map.set: an existing index keeps its slot). */
static bool config_set(qa_q2_server_demo_state *state, size_t *capacity, double index,
                       const char *value) {
  char *owned = dup_str(value);
  if (owned == NULL) return false;
  for (size_t i = 0; i < state->config_count; i++) {
    if (state->config_strings[i].index == index) {
      free(state->config_values[i]);
      state->config_values[i] = owned;
      state->config_strings[i].value = owned;
      return true;
    }
  }
  if (state->config_count == *capacity) {
    size_t next = *capacity > 0 ? *capacity * 2 : 16;
    qa_q2_server_demo_config *grown =
        (qa_q2_server_demo_config *)realloc(state->config_strings, next * sizeof *grown);
    if (grown == NULL) {
      free(owned);
      return false;
    }
    state->config_strings = grown;
    char **grown_values = (char **)realloc(state->config_values, next * sizeof *grown_values);
    if (grown_values == NULL) {
      free(owned);
      return false;
    }
    state->config_values = grown_values;
    *capacity = next;
  }
  state->config_strings[state->config_count].index = index;
  state->config_strings[state->config_count].value = owned;
  state->config_values[state->config_count] = owned;
  state->config_count++;
  return true;
}

/* Decodes one demo record into scope->record; false only on OOM. */
static bool read_one(demo_read_scope *scope, const qa_q2_demo_record *source, bool *signon) {
  qa_q2_wire_codec *wire = &scope->wire;
  qa_q2_server_demo_record *record = &scope->record;
  if (!qa_q2_wire_codec_begin(wire, source->bytes, source->bytes_length)) return false;
  qa_q2_sizebuf *message = &wire->message;
  int32_t opcode = qa_q2_msg_read_byte(message);
  if (opcode == 12) {
    if (qa_q2_msg_read_long(message) != 34) {
      qa_q2_throw_error("Server demo requires classic protocol34");
    }
    double servercount = (double)qa_q2_msg_read_long(message);
    if (qa_q2_msg_read_byte(message) != 2) qa_q2_throw_error("Not a native server demo");
    char gamedir[2048];
    (void)qa_q2_msg_read_string(message, gamedir, sizeof gamedir);
    if (qa_q2_msg_read_short(message) != -1) {
      qa_q2_throw_error("Server demo cannot contain a player view");
    }
    char ignored[2048];
    (void)qa_q2_msg_read_string(message, ignored, sizeof ignored);
    record->kind = QA_Q2_SERVER_DEMO_SIGNON;
    record->state.servercount = servercount;
    record->state.gamedir = dup_str(gamedir);
    if (record->state.gamedir == NULL) return false;
    size_t capacity = 0;
    while (message->readcount < message->cursize) {
      if (qa_q2_msg_read_byte(message) != 13) {
        qa_q2_throw_error("Invalid server demo signon opcode");
      }
      int32_t index = qa_q2_msg_read_short(message);
      if (index < 0 || index >= 2080) qa_q2_throw_error("Invalid server demo configstring");
      char value[2048];
      (void)qa_q2_msg_read_string(message, value, sizeof value);
      if (!config_set(&record->state, &capacity, (double)index, value)) return false;
      qa_q2_message_check_read(message);
    }
    qa_q2_wire_codec_finish(wire);
    *signon = true;
    return true;
  }
  if (opcode == 20 && *signon) {
    qa_q2_protocol_codec *codec = wire->selected_codec;
    double server_frame = (double)qa_q2_msg_read_long(message);
    record->kind = QA_Q2_SERVER_DEMO_FRAME;
    record->server_frame = server_frame;
    if (server_frame < 0) qa_q2_throw_error("Invalid server demo frame");
    codec->read_packet_entities_begin(codec);
    size_t capacity = 0;
    double previous = 0;
    for (;;) {
      qa_q2_entity_bits header;
      codec->read_entity_bits(codec, &header);
      qa_q2_message_check_read(message);
      if (header.number == 0) break;
      if (header.number <= previous || header.number >= 1024 ||
          (header.bits_u64 & (uint64_t)QA_Q2_U_REMOVE) != 0) {
        qa_q2_throw_error("Invalid full server demo entity");
      }
      previous = header.number;
      qa_q2_wire_entity empty;
      qa_q2_wire_entity_init(&empty);
      qa_q2_wire_entity entity;
      qa_q2_wire_entity_init(&entity);
      codec->read_delta_entity(codec, &empty, &entity, header.number, header.bits_u64);
      qa_q2_message_check_read(message);
      if (record->entity_count == capacity) {
        size_t next = capacity > 0 ? capacity * 2 : 16;
        qa_q2_wire_entity *grown =
            (qa_q2_wire_entity *)realloc(record->entities, next * sizeof *grown);
        if (grown == NULL) return false;
        record->entities = grown;
        capacity = next;
      }
      record->entities[record->entity_count++] = entity;
    }
    /* checkMessageRead above keeps readcount within the record. */
    size_t at = (size_t)message->readcount;
    return qa_q2_server_message_reader_read(&scope->multicast, source->bytes + at,
                                            source->bytes_length - at, &record->multicasts,
                                            &record->multicast_count);
  }
  qa_q2_throw_error("Server demo requires signon followed by entity frames");
}

bool qa_q2_read_server_demo(const uint8_t *bytes, size_t length,
                            qa_q2_server_demo_record **out, size_t *out_count) {
  qa_q2_protocol_identity classic;
  memset(&classic, 0, sizeof classic);
  classic.kind = "q2-classic";
  classic.version = 34;
  demo_read_scope scope;
  memset(&scope, 0, sizeof scope);
  qa_cleanup node;
  qa_cleanup_push(&node, demo_read_release, &scope);
  bool ok = qa_q2_wire_codec_init(&scope.wire, classic);
  scope.has_wire = ok;
  if (ok) {
    qa_q2_server_message_options options;
    memset(&options, 0, sizeof options);
    options.max_config_strings = 2080;
    options.inventory_slots = 256;
    ok = qa_q2_server_message_reader_init(&scope.multicast, scope.wire.selected_protocol,
                                          options);
    scope.has_multicast = ok;
  }
  if (ok) ok = qa_q2_read_demo(bytes, length, &scope.demo_records, &scope.demo_count);
  bool signon = false;
  for (size_t r = 0; ok && r < scope.demo_count; r++) {
    ok = read_one(&scope, &scope.demo_records[r], &signon);
    if (!ok) break;
    if (scope.count == scope.capacity) {
      size_t next = scope.capacity > 0 ? scope.capacity * 2 : 8;
      qa_q2_server_demo_record *grown =
          (qa_q2_server_demo_record *)realloc(scope.results, next * sizeof *grown);
      if (grown == NULL) {
        ok = false;
        break;
      }
      scope.results = grown;
      scope.capacity = next;
    }
    scope.results[scope.count++] = scope.record;
    memset(&scope.record, 0, sizeof scope.record);
  }
  if (ok && !signon) qa_q2_throw_error("Server demo has no signon");
  qa_cleanup_pop(&node);
  if (ok) {
    *out = scope.results;
    *out_count = scope.count;
    scope.results = NULL;
    scope.count = 0;
  }
  demo_read_release(&scope);
  return ok;
}
