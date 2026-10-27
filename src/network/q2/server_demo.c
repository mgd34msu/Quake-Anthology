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

uint8_t *qa_q2_encode_server_demo_signon(const qa_q2_server_demo_state *state,
                                         size_t *out_length) {
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, 32768)) return NULL;
  qa_q2_msg_write_byte(&message, 12);
  qa_q2_msg_write_long(&message, 34);
  qa_q2_msg_write_long(&message, state->servercount);
  qa_q2_msg_write_byte(&message, 2);
  qa_q2_msg_write_string(&message, state->gamedir);
  qa_q2_msg_write_short(&message, -1);
  const char *level = config_get(state, 0);
  qa_q2_msg_write_string(&message, level != NULL ? level : "");
  size_t count = state->config_count;
  qa_q2_server_demo_config *sorted = (qa_q2_server_demo_config *)malloc(
      count > 0 ? count * sizeof *sorted : sizeof *sorted);
  if (sorted == NULL) {
    qa_q2_message_destroy(&message);
    return NULL;
  }
  if (count > 0) memcpy(sorted, state->config_strings, count * sizeof *sorted);
  qsort(sorted, count, sizeof *sorted, config_compare);
  for (size_t i = 0; i < count; i++) {
    double index = sorted[i].index;
    if (index != trunc(index) || index < 0 || index >= 2080) {
      free(sorted);
      qa_q2_message_destroy(&message);
      qa_range_error("Classic server demo configstring outside range");
    }
    if (sorted[i].value == NULL || sorted[i].value[0] == '\0') continue;
    qa_q2_msg_write_byte(&message, 13);
    qa_q2_msg_write_short(&message, index);
    qa_q2_msg_write_string(&message, sorted[i].value);
  }
  free(sorted);
  uint8_t *out = qa_q2_message_bytes(&message, out_length);
  qa_q2_message_destroy(&message);
  return out;
}

uint8_t *qa_q2_encode_server_demo_frame(double server_frame,
                                        const qa_q2_wire_entity *entities,
                                        size_t entity_count,
                                        const qa_q2_byte_span *multicasts,
                                        size_t multicast_count, size_t *out_length) {
  if (server_frame != trunc(server_frame) || server_frame < 0 ||
      server_frame > 0x7fffffff) {
    qa_range_error("Invalid server demo frame");
  }
  qa_q2_protocol_identity classic;
  memset(&classic, 0, sizeof classic);
  classic.kind = "q2-classic";
  classic.version = 34;
  qa_q2_wire_codec wire;
  if (!qa_q2_wire_codec_init(&wire, classic)) return NULL;
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, 32768)) {
    qa_q2_wire_codec_destroy(&wire);
    return NULL;
  }
  qa_q2_wire_entity zero;
  qa_q2_wire_entity_init(&zero);
  qa_q2_msg_write_byte(&message, 20);
  qa_q2_msg_write_long(&message, server_frame);
  wire.selected_codec->write_packet_entities_begin(wire.selected_codec, &message);
  double previous = 0;
  for (size_t i = 0; i < entity_count; i++) {
    const qa_q2_wire_entity *entity = &entities[i];
    if (entity->number <= previous || entity->number >= 1024) {
      qa_q2_message_destroy(&message);
      qa_q2_wire_codec_destroy(&wire);
      qa_range_error("Classic server demo entities must be ordered and unique");
    }
    previous = entity->number;
    if (entity->modelindex != 0 || entity->effects != 0 || entity->sound != 0 ||
        entity->event != 0) {
      wire.selected_codec->write_delta_entity(wire.selected_codec, &message, &zero,
                                              entity, false, true);
    }
  }
  wire.selected_codec->write_packet_entities_end(wire.selected_codec, &message);
  for (size_t i = 0; i < multicast_count; i++) {
    qa_q2_sz_write(&message, multicasts[i].bytes, multicasts[i].length,
                   (int64_t)multicasts[i].length);
  }
  uint8_t *out = qa_q2_message_bytes(&message, out_length);
  qa_q2_message_destroy(&message);
  qa_q2_wire_codec_destroy(&wire);
  return out;
}

bool qa_q2_read_server_demo(const uint8_t *bytes, size_t length,
                            qa_q2_server_demo_record **out, size_t *out_count) {
  qa_q2_protocol_identity classic;
  memset(&classic, 0, sizeof classic);
  classic.kind = "q2-classic";
  classic.version = 34;
  qa_q2_wire_codec wire;
  if (!qa_q2_wire_codec_init(&wire, classic)) return false;
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = 2080;
  options.inventory_slots = 256;
  qa_q2_server_message_reader multicast;
  if (!qa_q2_server_message_reader_init(&multicast, wire.selected_protocol, options)) {
    qa_q2_wire_codec_destroy(&wire);
    return false;
  }
  qa_q2_demo_record *demo_records = NULL;
  size_t demo_count = 0;
  if (!qa_q2_read_demo(bytes, length, &demo_records, &demo_count)) {
    qa_q2_server_message_reader_destroy(&multicast);
    qa_q2_wire_codec_destroy(&wire);
    return false;
  }
  size_t capacity = 8;
  size_t count = 0;
  qa_q2_server_demo_record *results =
      (qa_q2_server_demo_record *)calloc(capacity, sizeof *results);
  if (results == NULL) {
    qa_q2_demo_records_free(demo_records, demo_count);
    qa_q2_server_message_reader_destroy(&multicast);
    qa_q2_wire_codec_destroy(&wire);
    return false;
  }
  bool signon = false;
  bool oom = false;
  for (size_t r = 0; r < demo_count && !oom; r++) {
    if (!qa_q2_wire_codec_begin(&wire, demo_records[r].bytes, demo_records[r].bytes_length)) {
      oom = true;
      break;
    }
    qa_q2_sizebuf *message = &wire.message;
    int32_t opcode = qa_q2_msg_read_byte(message);
    /* Multi-record loops free the growable array on failure below. */
    qa_q2_server_demo_record record;
    memset(&record, 0, sizeof record);
    if (opcode == 12) {
      if (qa_q2_msg_read_long(message) != 34) {
        qa_q2_server_demo_records_free(results, count);
        qa_q2_demo_records_free(demo_records, demo_count);
        qa_q2_server_message_reader_destroy(&multicast);
        qa_q2_wire_codec_destroy(&wire);
        qa_q2_throw_error("Server demo requires classic protocol34");
      }
      double servercount = (double)qa_q2_msg_read_long(message);
      if (qa_q2_msg_read_byte(message) != 2) {
        qa_q2_server_demo_records_free(results, count);
        qa_q2_demo_records_free(demo_records, demo_count);
        qa_q2_server_message_reader_destroy(&multicast);
        qa_q2_wire_codec_destroy(&wire);
        qa_q2_throw_error("Not a native server demo");
      }
      char gamedir[2048];
      (void)qa_q2_msg_read_string(message, gamedir, sizeof gamedir);
      if (qa_q2_msg_read_short(message) != -1) {
        qa_q2_server_demo_records_free(results, count);
        qa_q2_demo_records_free(demo_records, demo_count);
        qa_q2_server_message_reader_destroy(&multicast);
        qa_q2_wire_codec_destroy(&wire);
        qa_q2_throw_error("Server demo cannot contain a player view");
      }
      char ignored[2048];
      (void)qa_q2_msg_read_string(message, ignored, sizeof ignored);
      record.kind = QA_Q2_SERVER_DEMO_SIGNON;
      record.state.servercount = servercount;
      record.state.gamedir = dup_str(gamedir);
      size_t cfg_cap = 16;
      size_t cfg_count = 0;
      qa_q2_server_demo_config *configs =
          (qa_q2_server_demo_config *)calloc(cfg_cap, sizeof *configs);
      char **values = (char **)calloc(cfg_cap, sizeof(char *));
      if (record.state.gamedir == NULL || configs == NULL || values == NULL) {
        free(configs);
        free(values);
        state_free(&record.state);
        oom = true;
        break;
      }
      while (message->readcount < message->cursize && !oom) {
        if (qa_q2_msg_read_byte(message) != 13) {
          free(configs);
          for (size_t i = 0; i < cfg_count; i++) free(values[i]);
          free(values);
          state_free(&record.state);
          qa_q2_server_demo_records_free(results, count);
          qa_q2_demo_records_free(demo_records, demo_count);
          qa_q2_server_message_reader_destroy(&multicast);
          qa_q2_wire_codec_destroy(&wire);
          qa_q2_throw_error("Invalid server demo signon opcode");
        }
        int32_t index = qa_q2_msg_read_short(message);
        if (index < 0 || index >= 2080) {
          free(configs);
          for (size_t i = 0; i < cfg_count; i++) free(values[i]);
          free(values);
          state_free(&record.state);
          qa_q2_server_demo_records_free(results, count);
          qa_q2_demo_records_free(demo_records, demo_count);
          qa_q2_server_message_reader_destroy(&multicast);
          qa_q2_wire_codec_destroy(&wire);
          qa_q2_throw_error("Invalid server demo configstring");
        }
        char value[2048];
        (void)qa_q2_msg_read_string(message, value, sizeof value);
        qa_q2_message_check_read(message);
        /* Map.set: replace existing index. */
        long at = -1;
        for (size_t i = 0; i < cfg_count; i++) {
          if (configs[i].index == (double)index) at = (long)i;
        }
        char *owned = dup_str(value);
        if (owned == NULL) { oom = true; break; }
        if (at >= 0) {
          free(values[at]);
          values[at] = owned;
          configs[at].value = owned;
        } else {
          if (cfg_count == cfg_cap) {
            cfg_cap *= 2;
            qa_q2_server_demo_config *grown =
                (qa_q2_server_demo_config *)realloc(configs, cfg_cap * sizeof *grown);
            if (grown == NULL) {
              free(owned);
              oom = true;
              break;
            }
            configs = grown;
            char **grown_v = (char **)realloc(values, cfg_cap * sizeof(char *));
            if (grown_v == NULL) {
              free(owned);
              oom = true;
              break;
            }
            values = grown_v;
          }
          configs[cfg_count].index = (double)index;
          configs[cfg_count].value = owned;
          values[cfg_count] = owned;
          cfg_count++;
        }
      }
      if (oom) {
        for (size_t i = 0; i < cfg_count; i++) free(values[i]);
        free(values);
        free(configs);
        state_free(&record.state);
        break;
      }
      record.state.config_strings = configs;
      record.state.config_values = values;
      record.state.config_count = cfg_count;
      qa_q2_wire_codec_finish(&wire);
      signon = true;
    } else if (opcode == 20 && signon) {
      double server_frame = (double)qa_q2_msg_read_long(message);
      record.kind = QA_Q2_SERVER_DEMO_FRAME;
      record.server_frame = server_frame;
      if (server_frame < 0) {
        qa_q2_server_demo_records_free(results, count);
        qa_q2_demo_records_free(demo_records, demo_count);
        qa_q2_server_message_reader_destroy(&multicast);
        qa_q2_wire_codec_destroy(&wire);
        qa_q2_throw_error("Invalid server demo frame");
      }
      wire.selected_codec->read_packet_entities_begin(wire.selected_codec);
      size_t ent_cap = 16;
      size_t ent_count = 0;
      qa_q2_wire_entity *entities =
          (qa_q2_wire_entity *)calloc(ent_cap, sizeof *entities);
      if (entities == NULL) { oom = true; break; }
      double previous = 0;
      bool failed = false;
      for (;;) {
        qa_q2_entity_bits header;
        wire.selected_codec->read_entity_bits(wire.selected_codec, &header);
        qa_q2_message_check_read(message);
        if (header.number == 0) break;
        if (header.number <= previous || header.number >= 1024 ||
            (header.bits_u64 & QA_Q2_U_REMOVE) != 0) {
          free(entities);
          qa_q2_server_demo_records_free(results, count);
          qa_q2_demo_records_free(demo_records, demo_count);
          qa_q2_server_message_reader_destroy(&multicast);
          qa_q2_wire_codec_destroy(&wire);
          qa_q2_throw_error("Invalid full server demo entity");
        }
        previous = header.number;
        qa_q2_wire_entity empty;
        qa_q2_wire_entity_init(&empty);
        qa_q2_wire_entity entity;
        qa_q2_wire_entity_init(&entity);
        wire.selected_codec->read_delta_entity(wire.selected_codec, &empty, &entity,
                                               header.number, header.bits_u64);
        qa_q2_message_check_read(message);
        if (ent_count == ent_cap) {
          ent_cap *= 2;
          qa_q2_wire_entity *grown =
              (qa_q2_wire_entity *)realloc(entities, ent_cap * sizeof *grown);
          if (grown == NULL) { failed = true; break; }
          entities = grown;
        }
        entities[ent_count++] = entity;
      }
      if (failed) {
        free(entities);
        oom = true;
        break;
      }
      record.entities = entities;
      record.entity_count = ent_count;
      int64_t at = message->readcount;
      size_t tail_len = demo_records[r].bytes_length > (size_t)(at > 0 ? at : 0)
                            ? demo_records[r].bytes_length - (size_t)at
                            : 0;
      const uint8_t *tail = demo_records[r].bytes + (at > 0 ? (size_t)at : 0);
      if (!qa_q2_server_message_reader_read(&multicast, tail, tail_len,
                                            &record.multicasts,
                                            &record.multicast_count)) {
        record_dispose(&record);
        oom = true;
        break;
      }
    } else {
      qa_q2_server_demo_records_free(results, count);
      qa_q2_demo_records_free(demo_records, demo_count);
      qa_q2_server_message_reader_destroy(&multicast);
      qa_q2_wire_codec_destroy(&wire);
      qa_q2_throw_error("Server demo requires signon followed by entity frames");
    }
    if (count == capacity) {
      capacity *= 2;
      qa_q2_server_demo_record *grown =
          (qa_q2_server_demo_record *)realloc(results, capacity * sizeof *grown);
      if (grown == NULL) {
        record_dispose(&record);
        oom = true;
        break;
      }
      results = grown;
    }
    results[count++] = record;
  }
  qa_q2_demo_records_free(demo_records, demo_count);
  qa_q2_server_message_reader_destroy(&multicast);
  qa_q2_wire_codec_destroy(&wire);
  if (oom) {
    qa_q2_server_demo_records_free(results, count);
    return false;
  }
  if (!signon) {
    qa_q2_server_demo_records_free(results, count);
    qa_q2_throw_error("Server demo has no signon");
  }
  *out = results;
  *out_count = count;
  return true;
}
