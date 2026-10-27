/* Port of src/network/q2/mvd-playback.ts. GPL-2.0-or-later. */
#include "qa/network/q2/mvd_playback.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/codecs/mvd.h"
#include "qa/network/q2/codecs/q2pro_fields.h"
#include "qa/network/q2/constants.h"
#include "qa/network/q2/message.h"
#include "qa/network/q2/mvd_recording.h"

static char *dup_str(const char *s) {
  size_t n = strlen(s != NULL ? s : "");
  char *out = (char *)malloc(n + 1);
  if (out != NULL) memcpy(out, s != NULL ? s : "", n + 1);
  return out;
}

bool qa_q2_mvd_playback_init(qa_q2_mvd_playback *p,
                             qa_q2_mvd_visibility visibility) {
  memset(p, 0, sizeof *p);
  p->visibility = visibility;
  qa_q2_get_mvd_profile(2010, 0, &p->profile);
  p->selected = 0;
  p->dummy = -1;
  if (!qa_q2_wire_codec_init(&p->wire, p->profile.protocol)) return false;
  qa_q2repro_context_init(&p->rerelease, &p->wire.message);
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = 2080;
  options.inventory_slots = 256;
  if (!qa_q2_server_message_reader_init(&p->embedded, p->profile.protocol,
                                        options)) {
    qa_q2_wire_codec_destroy(&p->wire);
    return false;
  }
  return true;
}

void qa_q2_mvd_playback_destroy(qa_q2_mvd_playback *p) {
  for (size_t i = 0; i < p->config_string_count; i++) {
    free(p->config_strings[i].value);
  }
  free(p->config_strings);
  free(p->players);
  free(p->entities);
  free(p->portal_bits);
  qa_q2_server_message_reader_destroy(&p->embedded);
  qa_q2_wire_codec_destroy(&p->wire);
  memset(p, 0, sizeof *p);
}

qa_q2_protocol_identity qa_q2_mvd_playback_get_protocol(
    const qa_q2_mvd_playback *p) {
  return p->profile.protocol;
}
const qa_q2_server_data_result *qa_q2_mvd_playback_get_header(
    const qa_q2_mvd_playback *p) {
  return p->has_data ? &p->data : NULL;
}
double qa_q2_mvd_playback_get_selected_player(const qa_q2_mvd_playback *p) {
  return p->selected;
}
const qa_q2_wire_player *qa_q2_mvd_playback_player(
    const qa_q2_mvd_playback *p, double number) {
  for (size_t i = 0; i < p->player_count; i++) {
    if (p->players[i].number == number) return &p->players[i].player;
  }
  return NULL;
}
size_t qa_q2_mvd_playback_player_count(const qa_q2_mvd_playback *p) {
  return p->player_count;
}
const char *qa_q2_mvd_playback_config_string(const qa_q2_mvd_playback *p,
                                             double index) {
  for (size_t i = 0; i < p->config_string_count; i++) {
    if (p->config_strings[i].index == index) return p->config_strings[i].value;
  }
  return NULL;
}
void qa_q2_mvd_playback_select_player(qa_q2_mvd_playback *p, double number) {
  if (number != trunc(number) || number < 0 || number >= p->max_clients ||
      qa_q2_mvd_playback_player(p, number) == NULL) {
    qa_range_error("MVD player is not active");
  }
  p->selected = number;
}

static bool config_set(qa_q2_mvd_playback *p, double index, const char *value) {
  char *owned = dup_str(value);
  if (owned == NULL) return false;
  for (size_t i = 0; i < p->config_string_count; i++) {
    if (p->config_strings[i].index == index) {
      free(p->config_strings[i].value);
      p->config_strings[i].value = owned;
      return true;
    }
  }
  if (p->config_string_count == p->config_string_capacity) {
    size_t next = p->config_string_capacity > 0 ? p->config_string_capacity * 2 : 16;
    qa_q2_mvd_config_string *grown = (qa_q2_mvd_config_string *)realloc(
        p->config_strings, next * sizeof *grown);
    if (grown == NULL) {
      free(owned);
      return false;
    }
    p->config_strings = grown;
    p->config_string_capacity = next;
  }
  p->config_strings[p->config_string_count].index = index;
  p->config_strings[p->config_string_count].value = owned;
  p->config_string_count++;
  return true;
}
static bool player_set(qa_q2_mvd_playback *p, double number,
                       const qa_q2_wire_player *player) {
  for (size_t i = 0; i < p->player_count; i++) {
    if (p->players[i].number == number) {
      p->players[i].player = *player;
      return true;
    }
  }
  if (p->player_count == p->player_capacity) {
    size_t next = p->player_capacity > 0 ? p->player_capacity * 2 : 16;
    qa_q2_mvd_capture_player *grown =
        (qa_q2_mvd_capture_player *)realloc(p->players, next * sizeof *grown);
    if (grown == NULL) return false;
    p->players = grown;
    p->player_capacity = next;
  }
  p->players[p->player_count].number = number;
  p->players[p->player_count].player = *player;
  p->player_count++;
  return true;
}
static void player_delete(qa_q2_mvd_playback *p, double number) {
  for (size_t i = 0; i < p->player_count; i++) {
    if (p->players[i].number == number) {
      memmove(&p->players[i], &p->players[i + 1],
              (p->player_count - i - 1) * sizeof *p->players);
      p->player_count--;
      return;
    }
  }
}
static void selected_or_first(qa_q2_mvd_playback *p) {
  if (qa_q2_mvd_playback_player(p, p->selected) == NULL) {
    p->selected = p->player_count > 0 ? p->players[0].number : 0;
  }
}
static qa_q2_wire_player current_player(qa_q2_mvd_playback *p) {
  const qa_q2_wire_player *found = qa_q2_mvd_playback_player(p, p->selected);
  if (found != NULL) return *found;
  qa_q2_wire_player empty;
  qa_q2_wire_player_init(&empty);
  return empty;
}

static int entity_cmp(const void *a, const void *b) {
  double d = ((const qa_q2_wire_entity *)a)->number -
             ((const qa_q2_wire_entity *)b)->number;
  return d < 0 ? -1 : d > 0 ? 1 : 0;
}
/* TS frame(). False only on OOM. */
static bool build_frame(qa_q2_mvd_playback *p, qa_q2_wire_frame *out) {
  memset(out, 0, sizeof *out);
  out->server_frame = p->frame_number;
  out->delta_frame = -1;
  out->suppressed_count = 0;
  out->player = current_player(p);
  size_t area_length = 0;
  out->area_bits = p->visibility.area_bits(
      p->visibility.ctx, &out->player, p->portal_bits, p->portal_bits_length,
      &area_length);
  if (out->area_bits == NULL && area_length > 0) return false;
  out->area_bits_length = area_length;
  qa_q2_wire_entity *sorted = NULL;
  if (p->entity_count > 0) {
    sorted = (qa_q2_wire_entity *)malloc(p->entity_count * sizeof *sorted);
    if (sorted == NULL) {
      qa_q2_wire_frame_free(out);
      return false;
    }
    memcpy(sorted, p->entities, p->entity_count * sizeof *sorted);
    qsort(sorted, p->entity_count, sizeof *sorted, entity_cmp);
  }
  size_t kept = p->visibility.entities(p->visibility.ctx, sorted, p->entity_count,
                                       &out->player, p->portal_bits,
                                       p->portal_bits_length);
  out->entities = sorted;
  out->entity_count = kept;
  return true;
}

bool qa_q2_mvd_playback_projection(qa_q2_mvd_playback *p, double *out_clientnum,
                                   qa_q2_wire_frame *out_frame) {
  if (out_clientnum != NULL) *out_clientnum = p->selected;
  return build_frame(p, out_frame);
}

static void read_entity(qa_q2_mvd_playback *p, const qa_q2_wire_entity *from,
                        qa_q2_wire_entity *to, double number, uint64_t bits) {
  if (p->profile.rerelease) {
    p->rerelease.codec.read_delta_entity(&p->rerelease.codec, from, to, number,
                                         bits);
  } else if (p->profile.extended) {
    qa_q2pro_features features;
    features.revision = p->profile.fog ? 1026 : p->profile.v2 ? 1025 : 1024;
    features.flags = p->profile.v2 ? 24 : 8;
    qa_q2pro_read_entity(&p->wire.message, features, from, to, number, bits);
  } else {
    qa_q2_protocol_codec *codec = qa_q2_wire_codec_get_codec(&p->wire);
    codec->read_delta_entity(codec, from, to, number, bits);
  }
}

typedef struct entity_list {
  qa_q2_wire_entity *items;
  size_t count, capacity;
} entity_list;
static bool entity_list_set(entity_list *list, const qa_q2_wire_entity *entity) {
  for (size_t i = 0; i < list->count; i++) {
    if (list->items[i].number == entity->number) {
      list->items[i] = *entity;
      return true;
    }
  }
  if (list->count == list->capacity) {
    size_t next = list->capacity > 0 ? list->capacity * 2 : 16;
    qa_q2_wire_entity *grown =
        (qa_q2_wire_entity *)realloc(list->items, next * sizeof *grown);
    if (grown == NULL) return false;
    list->items = grown;
    list->capacity = next;
  }
  list->items[list->count++] = *entity;
  return true;
}
static qa_q2_wire_entity *entity_list_get(entity_list *list, double number) {
  for (size_t i = 0; i < list->count; i++) {
    if (list->items[i].number == number) return &list->items[i];
  }
  return NULL;
}
static void entity_list_delete(entity_list *list, double number) {
  for (size_t i = 0; i < list->count; i++) {
    if (list->items[i].number == number) {
      memmove(&list->items[i], &list->items[i + 1],
              (list->count - i - 1) * sizeof *list->items);
      list->count--;
      return;
    }
  }
}

/* TS readFrame(dummy). False only on OOM (TS throws abort). */
static bool read_frame(qa_q2_mvd_playback *p, double dummy) {
  qa_q2_sizebuf *message = &p->wire.message;
  int32_t portal_length = qa_q2_msg_read_byte(message);
  if (portal_length < 0 ||
      message->readcount + portal_length > message->cursize) {
    qa_q2_throw_error("Truncated MVD portal bits");
  }
  uint8_t *portal = (uint8_t *)malloc(portal_length > 0 ? (size_t)portal_length : 1);
  if (portal == NULL) return false;
  if (portal_length > 0) {
    memcpy(portal, message->data + message->readcount, (size_t)portal_length);
  }
  free(p->portal_bits);
  p->portal_bits = portal;
  p->portal_bits_length = (size_t)portal_length;
  message->readcount += portal_length;
  for (;;) {
    int32_t number = qa_q2_msg_read_byte(message);
    if (number == QA_Q2_MVD_CLIENTNUM_NONE) break;
    if (number < 0 || (double)number >= p->max_clients) {
      qa_q2_throw_error("Invalid MVD player number");
    }
    const qa_q2_wire_player *from = qa_q2_mvd_playback_player(p, number);
    qa_q2_wire_player from_copy;
    const qa_q2_wire_player *from_ptr = NULL;
    if (from != NULL) {
      from_copy = *from;
      from_ptr = &from_copy;
    }
    qa_q2_mvd_player_result result;
    qa_q2_mvd_read_player(message, from_ptr, number, p->profile, &result);
    if (result.removed) {
      player_delete(p, number);
    } else {
      result.ps.clientnum = number;
      if (!player_set(p, number, &result.ps)) return false;
    }
  }
  entity_list next = {NULL, 0, 0};
  for (size_t i = 0; i < p->entity_count; i++) {
    qa_q2_wire_entity previous = p->entities[i];
    qa_q2_wire_entity entity;
    qa_q2_wire_entity_init(&entity);
    read_entity(p, &previous, &entity, previous.number, 0);
    const float *old_origin =
        ((int32_t)previous.renderfx & 128) ? previous.old_origin : previous.origin;
    entity.old_origin[0] = old_origin[0];
    entity.old_origin[1] = old_origin[1];
    entity.old_origin[2] = old_origin[2];
    if (!entity_list_set(&next, &entity)) {
      free(next.items);
      return false;
    }
  }
  for (;;) {
    qa_q2_entity_bits header;
    if (p->profile.extended) {
      qa_q2pro_read_entity_bits(message, &header);
    } else {
      qa_q2_protocol_codec *codec = qa_q2_wire_codec_get_codec(&p->wire);
      codec->read_entity_bits(codec, &header);
    }
    qa_q2_message_check_read(message);
    double number = header.number;
    uint64_t bits = header.bits_u64;
    if (number == 0) break;
    if (number < 0 || number >= p->profile.max_entities) {
      qa_q2_throw_error("Invalid MVD entity number");
    }
    if (bits & QA_Q2_U_REMOVE) {
      entity_list_delete(&next, number);
      continue;
    }
    qa_q2_wire_entity previous;
    qa_q2_wire_entity_init(&previous);
    const qa_q2_wire_entity *found = NULL;
    for (size_t i = 0; i < p->entity_count; i++) {
      if (p->entities[i].number == number) found = &p->entities[i];
    }
    if (found != NULL) previous = *found;
    qa_q2_wire_entity entity;
    qa_q2_wire_entity_init(&entity);
    read_entity(p, &previous, &entity, number, bits);
    if (!(bits & QA_Q2_U_OLDORIGIN)) {
      const float *old_origin =
          ((int32_t)previous.renderfx & 128) ? previous.old_origin : previous.origin;
      entity.old_origin[0] = old_origin[0];
      entity.old_origin[1] = old_origin[1];
      entity.old_origin[2] = old_origin[2];
    }
    if (bits & QA_Q2_U_FRAME16) entity.frame = (double)((int32_t)entity.frame & 65535);
    if ((bits & (QA_Q2_U_SKIN8 | QA_Q2_U_SKIN16)) == QA_Q2_U_SKIN16) {
      entity.skinnum = (double)((int32_t)entity.skinnum & 65535);
    }
    if ((bits & (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) == QA_Q2_U_EFFECTS16) {
      entity.effects = (double)((int32_t)entity.effects & 65535);
    }
    if ((bits & (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) == QA_Q2_U_RENDERFX16) {
      entity.renderfx = (double)((int32_t)entity.renderfx & 65535);
    }
    if (!p->profile.extended && (bits & QA_Q2_U_SOLID)) {
      entity.solid = (double)((int32_t)entity.solid & 65535);
    }
    if (!entity_list_set(&next, &entity)) {
      free(next.items);
      return false;
    }
  }
  for (size_t i = 0; i < p->player_count; i++) {
    double number = p->players[i].number;
    qa_q2_wire_player player = p->players[i].player;
    qa_q2_wire_entity *entity = entity_list_get(&next, number + 1);
    if (entity == NULL || number == dummy || player.pmove.pm_type != 0) continue;
    for (int axis = 0; axis < 3; axis++) {
      entity->origin[axis] = (float)(p->profile.rerelease
                                         ? (double)player.pmove.origin_f[axis]
                                         : (double)player.pmove.origin[axis] / 8);
    }
    double pitch = (double)player.viewangles[0];
    entity->angles[0] = (float)((pitch > 180 ? pitch - 360 : pitch) / 3);
    entity->angles[1] = player.viewangles[1];
    entity->angles[2] = 0;
  }
  free(p->entities);
  p->entities = next.items;
  p->entity_count = next.count;
  p->entity_capacity = next.capacity;
  selected_or_first(p);
  p->frame_number++;
  return true;
}

typedef struct record_list {
  qa_q2_server_record *items;
  size_t count, capacity;
} record_list;
static bool records_push(record_list *list, qa_q2_server_record *record) {
  if (list->count == list->capacity) {
    size_t next = list->capacity > 0 ? list->capacity * 2 : 16;
    qa_q2_server_record *grown =
        (qa_q2_server_record *)realloc(list->items, next * sizeof *grown);
    if (grown == NULL) return false;
    list->items = grown;
    list->capacity = next;
  }
  list->items[list->count++] = *record;
  memset(record, 0, sizeof *record);
  return true;
}
/* TS emit(): record with raw span [start, readcount). Takes event ownership. */
static bool emit_record(qa_q2_mvd_playback *p, record_list *list, double opcode,
                        int64_t start, qa_q2_server_event *event) {
  qa_q2_message_check_read(&p->wire.message);
  qa_q2_server_record record;
  memset(&record, 0, sizeof record);
  record.seat = 0;
  record.opcode = opcode;
  size_t length = (size_t)(p->wire.message.readcount - start);
  record.raw = (uint8_t *)malloc(length > 0 ? length : 1);
  if (record.raw == NULL) {
    qa_q2_server_event_clear(event);
    return false;
  }
  if (length > 0) memcpy(record.raw, p->wire.message.data + start, length);
  record.raw_length = length;
  record.event = *event;
  memset(event, 0, sizeof *event);
  if (!records_push(list, &record)) {
    qa_q2_server_records_free(&record, 1);
    return false;
  }
  return true;
}
static void number_text(double value, char *buffer, size_t capacity) {
  if (value == (double)(int64_t)value) {
    (void)snprintf(buffer, capacity, "%lld", (long long)(int64_t)value);
  } else {
    (void)snprintf(buffer, capacity, "%g", value);
  }
}
static bool embedded_init(qa_q2_mvd_playback *p, qa_q2_protocol_identity protocol,
                          double max_config_strings) {
  qa_q2_server_message_reader_destroy(&p->embedded);
  qa_q2_server_message_options options;
  memset(&options, 0, sizeof options);
  options.max_config_strings = max_config_strings;
  options.inventory_slots = 256;
  return qa_q2_server_message_reader_init(&p->embedded, protocol, options);
}

bool qa_q2_mvd_playback_read(qa_q2_mvd_playback *p, const uint8_t *bytes,
                             size_t length, qa_q2_server_record **out,
                             size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  if (length < 1 || length > QA_Q2_MVD_MAX_MESSAGE) {
    qa_range_error("Invalid MVD message length");
  }
  if (!qa_q2_wire_codec_begin(&p->wire, bytes, length)) return false;
  qa_q2_sizebuf *message = &p->wire.message;
  record_list records = {NULL, 0, 0};
  bool ok = true;
  while (message->readcount < message->cursize && ok) {
    int64_t start = message->readcount;
    qa_q2_mvd_cmd command;
    qa_q2_msg_read_mvd_cmd(message, &command);
    int32_t op = (int32_t)command.op;
    if (op == QA_Q2_MVD_NOP) {
      /* break */
    } else if (op == QA_Q2_MVD_SERVERDATA) {
      qa_q2_mvd_header header;
      if (!qa_q2_read_mvd_header(bytes + start, length - (size_t)start, &header)) {
        ok = false;
        break;
      }
      p->profile = header.profile;
      message->readcount = start + (int64_t)header.frame_offset;
      if (!embedded_init(p, header.profile.protocol,
                         header.profile.max_config_strings)) {
        qa_q2_mvd_header_free(&header);
        ok = false;
        break;
      }
      if (strcmp(header.profile.protocol.kind, "q2-q2pro") == 0) {
        qa_q2_wire_codec_accept_q2pro_features(
            &p->embedded.wire, (double)header.profile.protocol.revision,
            header.data.has_wire_flags ? header.data.wire_flags : 0);
      }
      for (size_t i = 0; i < p->config_string_count; i++) {
        free(p->config_strings[i].value);
      }
      p->config_string_count = 0;
      p->player_count = 0;
      p->entity_count = 0;
      p->frame_number = 0;
      for (size_t i = 0; i < header.config_string_count && ok; i++) {
        ok = config_set(p, header.config_strings[i].index,
                        header.config_strings[i].value);
      }
      p->max_clients = header.max_clients;
      p->dummy = header.dummy;
      qa_q2_server_data_result data = header.data;
      qa_q2_mvd_header_free(&header);
      if (!ok) break;
      if (!read_frame(p, p->dummy)) {
        ok = false;
        break;
      }
      selected_or_first(p);
      data.clientnum = p->selected;
      p->data = data;
      p->has_data = true;
      qa_q2_server_event event;
      memset(&event, 0, sizeof event);
      event.kind = QA_Q2_SERVER_DATA;
      event.data = data;
      ok = emit_record(p, &records, 12, start, &event);
      for (size_t i = 0; i < p->config_string_count && ok; i++) {
        memset(&event, 0, sizeof event);
        event.kind = QA_Q2_SERVER_CONFIG_STRING;
        event.index = p->config_strings[i].index;
        event.text = dup_str(p->config_strings[i].value);
        if (event.text == NULL) {
          ok = false;
          break;
        }
        ok = emit_record(p, &records, 13, start, &event);
      }
      if (ok) {
        char count_text[64];
        number_text(data.servercount, count_text, sizeof count_text);
        char text[96];
        (void)snprintf(text, sizeof text, "precache %s\n", count_text);
        memset(&event, 0, sizeof event);
        event.kind = QA_Q2_SERVER_COMMAND_TEXT;
        event.text = dup_str(text);
        if (event.text == NULL) {
          ok = false;
          break;
        }
        ok = emit_record(p, &records, 11, start, &event);
      }
      if (ok) {
        memset(&event, 0, sizeof event);
        event.kind = QA_Q2_SERVER_FRAME;
        if (!build_frame(p, &event.frame)) {
          ok = false;
          break;
        }
        ok = emit_record(p, &records, 20, start, &event);
      }
    } else if (op == QA_Q2_MVD_FRAME) {
      if (!p->has_data) qa_q2_throw_error("MVD frame before serverdata");
      if (!read_frame(p, p->dummy)) {
        ok = false;
        break;
      }
      qa_q2_server_event event;
      memset(&event, 0, sizeof event);
      event.kind = QA_Q2_SERVER_FRAME;
      if (!build_frame(p, &event.frame)) {
        ok = false;
        break;
      }
      ok = emit_record(p, &records, 20, start, &event);
    } else if (op == QA_Q2_MVD_CONFIGSTRING) {
      double index = (double)qa_q2_msg_read_word(message);
      char value[2048];
      qa_q2_msg_read_string(message, value, sizeof value);
      if (index >= p->profile.max_config_strings) {
        qa_q2_throw_error("Invalid MVD configstring index");
      }
      if (!config_set(p, index, value)) {
        ok = false;
        break;
      }
      qa_q2_server_event event;
      memset(&event, 0, sizeof event);
      event.kind = QA_Q2_SERVER_CONFIG_STRING;
      event.index = index;
      event.text = dup_str(value);
      if (event.text == NULL) {
        ok = false;
        break;
      }
      ok = emit_record(p, &records, 13, start, &event);
    } else if (op == QA_Q2_MVD_PRINT) {
      double level = (double)qa_q2_msg_read_byte(message);
      char text[2048];
      qa_q2_msg_read_string(message, text, sizeof text);
      qa_q2_server_event event;
      memset(&event, 0, sizeof event);
      event.kind = QA_Q2_SERVER_PRINT;
      event.level = level;
      event.text = dup_str(text);
      if (event.text == NULL) {
        ok = false;
        break;
      }
      ok = emit_record(p, &records, 10, start, &event);
    } else if (op == QA_Q2_MVD_SOUND) {
      int32_t flags = qa_q2_msg_read_byte(message);
      double index = (flags & 32) ? (double)qa_q2_msg_read_word(message)
                                  : (double)qa_q2_msg_read_byte(message);
      double volume = (flags & 1) ? (double)qa_q2_msg_read_byte(message) / 255 : 1;
      double attenuation =
          (flags & 2) ? (double)qa_q2_msg_read_byte(message) / 64 : 1;
      double delay =
          (flags & 16) ? (double)qa_q2_msg_read_byte(message) / 1000 : 0;
      int32_t channel_word = qa_q2_msg_read_word(message);
      double entity_number = (double)((uint32_t)channel_word >> 3);
      if (entity_number >= p->profile.max_entities) {
        qa_q2_throw_error("Invalid MVD sound entity");
      }
      const qa_q2_wire_entity *state = NULL;
      for (size_t i = 0; i < p->entity_count; i++) {
        if (p->entities[i].number == entity_number) state = &p->entities[i];
      }
      if (state != NULL) {
        qa_q2_wire_entity state_copy = *state;
        float position[3] = {0, 0, 0};
        p->visibility.sound_origin(p->visibility.ctx, &state_copy, position);
        qa_q2_wire_player player = current_player(p);
        if (((int32_t)command.extrabits & 1) != 0 ||
            p->visibility.sound_audible(p->visibility.ctx, position, &player,
                                        p->portal_bits, p->portal_bits_length)) {
          qa_q2_server_event event;
          memset(&event, 0, sizeof event);
          event.kind = QA_Q2_SERVER_SOUND;
          event.sound.flags = (double)(flags | 12);
          event.sound.index = index;
          event.sound.volume = volume;
          event.sound.attenuation = attenuation;
          event.sound.delay_seconds = delay;
          event.sound.entity = entity_number;
          event.sound.channel = (double)(channel_word & 7);
          event.sound.has_position = true;
          event.sound.position.x = (double)position[0];
          event.sound.position.y = (double)position[1];
          event.sound.position.z = (double)position[2];
          ok = emit_record(p, &records, 9, start, &event);
        }
      }
    } else if (op == QA_Q2_MVD_UNICAST || op == QA_Q2_MVD_UNICAST_R ||
               op == QA_Q2_MVD_MULTICAST_ALL || op == QA_Q2_MVD_MULTICAST_ALL_R ||
               op == QA_Q2_MVD_MULTICAST_PVS || op == QA_Q2_MVD_MULTICAST_PVS_R ||
               op == QA_Q2_MVD_MULTICAST_PHS || op == QA_Q2_MVD_MULTICAST_PHS_R) {
      int64_t payload_length =
          qa_q2_msg_read_byte(message) | ((int64_t)command.extrabits << 8);
      bool visible = true;
      if (op == QA_Q2_MVD_UNICAST || op == QA_Q2_MVD_UNICAST_R) {
        int32_t number = qa_q2_msg_read_byte(message);
        if ((double)number >= p->max_clients) {
          qa_q2_throw_error("Invalid MVD unicast player");
        }
        visible = (double)number == p->selected;
      } else if (op != QA_Q2_MVD_MULTICAST_ALL && op != QA_Q2_MVD_MULTICAST_ALL_R) {
        double leaf = (double)qa_q2_msg_read_word(message);
        bool is_pvs = op == QA_Q2_MVD_MULTICAST_PVS || op == QA_Q2_MVD_MULTICAST_PVS_R;
        qa_q2_wire_player player = current_player(p);
        visible = p->visibility.visible(p->visibility.ctx, leaf, is_pvs, &player,
                                        p->portal_bits, p->portal_bits_length);
      }
      int64_t end = message->readcount + payload_length;
      if (end > message->cursize) {
        qa_q2_throw_error("Truncated MVD embedded message");
      }
      const uint8_t *payload = message->data + message->readcount;
      size_t payload_len = (size_t)payload_length;
      /* Copy: embedded read may not touch wire.message, but payload
       * points into it; readcount advances before the nested read. */
      uint8_t *copy = (uint8_t *)malloc(payload_len > 0 ? payload_len : 1);
      if (copy == NULL) {
        ok = false;
        break;
      }
      if (payload_len > 0) memcpy(copy, payload, payload_len);
      message->readcount = end;
      if (visible) {
        qa_q2_server_record *nested = NULL;
        size_t nested_count = 0;
        if (!qa_q2_server_message_reader_read(&p->embedded, copy, payload_len,
                                              &nested, &nested_count)) {
          free(copy);
          ok = false;
          break;
        }
        for (size_t i = 0; i < nested_count && ok; i++) {
          ok = records_push(&records, &nested[i]);
        }
        free(nested);
      }
      free(copy);
    } else {
      qa_q2_throw_error("Unsupported MVD command %g", command.op);
    }
    qa_q2_message_check_read(message);
  }
  qa_q2_wire_codec_finish(&p->wire);
  if (!ok) {
    qa_q2_server_records_free(records.items, records.count);
    return false;
  }
  *out = records.items;
  *out_count = records.count;
  return true;
}
