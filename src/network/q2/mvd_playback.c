/* Port of src/network/q2/mvd-playback.ts. GPL-2.0-or-later. */
#include "qa/network/common/js_number.h"
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
#include "qa/foundation/js.h"

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
static void frame_release(void *arg) { qa_q2_wire_frame_free((qa_q2_wire_frame *)arg); }
/* TS frame(). False only on OOM. The visibility callbacks may raise (TS throws through
 * frame()); the partly built frame is released by the cleanup then. */
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
  qa_cleanup node;
  qa_cleanup_push(&node, frame_release, out);
  if (p->entity_count > 0) {
    out->entities = (qa_q2_wire_entity *)malloc(p->entity_count * sizeof *out->entities);
    if (out->entities == NULL) {
      qa_cleanup_pop(&node);
      qa_q2_wire_frame_free(out);
      return false;
    }
    memcpy(out->entities, p->entities, p->entity_count * sizeof *out->entities);
    qa_js_sort(out->entities, p->entity_count, sizeof *out->entities, entity_cmp);
  }
  size_t kept = p->visibility.entities(p->visibility.ctx, out->entities, p->entity_count,
                                       &out->player, p->portal_bits, p->portal_bits_length);
  qa_cleanup_pop(&node);
  out->entity_count = kept <= p->entity_count ? kept : p->entity_count;
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
static void entity_list_release(void *arg) {
  entity_list *list = (entity_list *)arg;
  free(list->items);
  list->items = NULL;
  list->count = list->capacity = 0;
}
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
static void carry_old_origin(qa_q2_wire_entity *entity, const qa_q2_wire_entity *previous) {
  const float *old_origin =
      (qa_js_to_int32(previous->renderfx) & 128) ? previous->old_origin : previous->origin;
  entity->old_origin[0] = old_origin[0];
  entity->old_origin[1] = old_origin[1];
  entity->old_origin[2] = old_origin[2];
}

/* TS readFrame(dummy). False only on OOM. Players and portal bits are committed as read,
 * as in TS; the entity Map is replaced only when the frame completes. */
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
  qa_cleanup node;
  qa_cleanup_push(&node, entity_list_release, &next);
  for (size_t i = 0; i < p->entity_count; i++) {
    qa_q2_wire_entity previous = p->entities[i];
    qa_q2_wire_entity entity;
    qa_q2_wire_entity_init(&entity);
    read_entity(p, &previous, &entity, previous.number, 0);
    carry_old_origin(&entity, &previous);
    if (!entity_list_set(&next, &entity)) {
      qa_cleanup_pop(&node);
      entity_list_release(&next);
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
    for (size_t i = 0; i < p->entity_count; i++) {
      if (p->entities[i].number == number) {
        previous = p->entities[i];
        break;
      }
    }
    qa_q2_wire_entity entity;
    qa_q2_wire_entity_init(&entity);
    read_entity(p, &previous, &entity, number, bits);
    if (!(bits & QA_Q2_U_OLDORIGIN)) carry_old_origin(&entity, &previous);
    if (bits & QA_Q2_U_FRAME16) entity.frame = (double)(qa_js_to_int32(entity.frame) & 65535);
    if ((bits & (QA_Q2_U_SKIN8 | QA_Q2_U_SKIN16)) == QA_Q2_U_SKIN16) {
      entity.skinnum = (double)(qa_js_to_int32(entity.skinnum) & 65535);
    }
    if ((bits & (QA_Q2_U_EFFECTS8 | QA_Q2_U_EFFECTS16)) == QA_Q2_U_EFFECTS16) {
      entity.effects = (double)(qa_js_to_int32(entity.effects) & 65535);
    }
    if ((bits & (QA_Q2_U_RENDERFX8 | QA_Q2_U_RENDERFX16)) == QA_Q2_U_RENDERFX16) {
      entity.renderfx = (double)(qa_js_to_int32(entity.renderfx) & 65535);
    }
    if (!p->profile.extended && (bits & QA_Q2_U_SOLID)) {
      entity.solid = (double)(qa_js_to_int32(entity.solid) & 65535);
    }
    if (!entity_list_set(&next, &entity)) {
      qa_cleanup_pop(&node);
      entity_list_release(&next);
      return false;
    }
  }
  qa_cleanup_pop(&node);
  for (size_t i = 0; i < p->player_count; i++) {
    double number = p->players[i].number;
    const qa_q2_wire_player *player = &p->players[i].player;
    qa_q2_wire_entity *entity = entity_list_get(&next, number + 1);
    if (entity == NULL || number == dummy || player->pmove.pm_type != 0) continue;
    for (int axis = 0; axis < 3; axis++) {
      entity->origin[axis] = (float)(p->profile.rerelease
                                         ? (double)player->pmove.origin_f[axis]
                                         : (double)player->pmove.origin[axis] / 8);
    }
    double pitch = (double)player->viewangles[0];
    entity->angles[0] = (float)((pitch > 180 ? pitch - 360 : pitch) / 3);
    entity->angles[1] = player->viewangles[1];
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

/* The TS `records` array of one readRecords call; raised reads release it. */
typedef struct record_list {
  qa_q2_server_record *items;
  size_t count, capacity;
} record_list;
static void record_list_release(void *arg) {
  record_list *list = (record_list *)arg;
  qa_q2_server_records_free(list->items, list->count);
  list->items = NULL;
  list->count = list->capacity = 0;
}
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
static void event_release(void *arg) { qa_q2_server_event_clear((qa_q2_server_event *)arg); }
/* TS emit(): record with raw span [start, readcount). Takes event ownership, also when
 * checkMessageRead raises. */
static bool emit_record(qa_q2_mvd_playback *p, record_list *list, double opcode,
                        int64_t start, qa_q2_server_event *event) {
  qa_cleanup node;
  qa_cleanup_push(&node, event_release, event);
  qa_q2_message_check_read(&p->wire.message);
  qa_cleanup_pop(&node);
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
/* Owned text event (config-string, print, command-text). False only on OOM. */
static bool emit_text(qa_q2_mvd_playback *p, record_list *list, double opcode, int64_t start,
                      qa_q2_server_event *event, const char *text) {
  event->text = dup_str(text);
  if (event->text == NULL) return false;
  return emit_record(p, list, opcode, start, event);
}
static bool emit_frame(qa_q2_mvd_playback *p, record_list *list, int64_t start) {
  qa_q2_server_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_FRAME;
  if (!build_frame(p, &event.frame)) return false;
  return emit_record(p, list, 20, start, &event);
}
/* TS `yield* records; records.length = 0`: ownership of each record passes to the sink. */
static void flush_records(record_list *list, qa_q2_mvd_record_sink sink, void *ctx) {
  size_t count = list->count;
  qa_q2_server_record *items = list->items;
  list->items = NULL;
  list->count = list->capacity = 0;
  /* Records not yet handed over when the sink raises stay owned here. */
  record_list rest = {items, count, count};
  qa_cleanup node;
  qa_cleanup_push(&node, record_list_release, &rest);
  for (size_t i = 0; i < count; i++) {
    qa_q2_server_record record = items[i];
    memset(&items[i], 0, sizeof items[i]);
    sink(ctx, &record);
  }
  qa_cleanup_pop(&node);
  free(items);
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
static void header_release(void *arg) { qa_q2_mvd_header_free((qa_q2_mvd_header *)arg); }
static void bytes_release(void *arg) {
  uint8_t **bytes = (uint8_t **)arg;
  free(*bytes);
  *bytes = NULL;
}

/* TS mvd_serverdata case. False only on OOM. */
static bool read_serverdata(qa_q2_mvd_playback *p, const uint8_t *bytes, size_t length,
                            int64_t start, record_list *records,
                            qa_q2_mvd_record_sink sink, void *ctx) {
  qa_q2_mvd_header header;
  if (!qa_q2_read_mvd_header(bytes + start, length - (size_t)start, &header)) return false;
  qa_cleanup node;
  qa_cleanup_push(&node, header_release, &header);
  p->profile = header.profile;
  p->wire.message.readcount = start + (int64_t)header.frame_offset;
  bool ok = embedded_init(p, header.profile.protocol, header.profile.max_config_strings);
  if (ok && strcmp(header.profile.protocol.kind, "q2-q2pro") == 0) {
    qa_q2_wire_codec_accept_q2pro_features(
        &p->embedded.wire, (double)header.profile.protocol.revision,
        header.data.has_wire_flags ? header.data.wire_flags : 0);
  }
  if (ok) {
    for (size_t i = 0; i < p->config_string_count; i++) free(p->config_strings[i].value);
    p->config_string_count = 0;
    p->player_count = 0;
    p->entity_count = 0;
    p->frame_number = 0;
    for (size_t i = 0; i < header.config_string_count && ok; i++) {
      ok = config_set(p, header.config_strings[i].index, header.config_strings[i].value);
    }
    p->max_clients = header.max_clients;
    p->dummy = header.dummy;
  }
  qa_q2_server_data_result data = header.data;
  qa_cleanup_pop(&node);
  qa_q2_mvd_header_free(&header);
  if (!ok || !read_frame(p, p->dummy)) return false;
  selected_or_first(p);
  data.clientnum = p->selected;
  p->data = data;
  p->has_data = true;
  qa_q2_server_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_DATA;
  event.data = data;
  if (!emit_record(p, records, 12, start, &event)) return false;
  for (size_t i = 0; i < p->config_string_count; i++) {
    memset(&event, 0, sizeof event);
    event.kind = QA_Q2_SERVER_CONFIG_STRING;
    event.index = p->config_strings[i].index;
    if (!emit_text(p, records, 13, start, &event, p->config_strings[i].value)) return false;
  }
  /* Full state is ready before normal receiver precache and first frame publication. */
  char count_text[64];
  qa_net_js_number(data.servercount, count_text, sizeof count_text);
  char text[96];
  (void)snprintf(text, sizeof text, "precache %s\n", count_text);
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_SERVER_COMMAND_TEXT;
  if (!emit_text(p, records, 11, start, &event, text)) return false;
  flush_records(records, sink, ctx);
  return emit_frame(p, records, start);
}

/* TS mvd_unicast/multicast cases. False only on OOM. */
static bool read_routed(qa_q2_mvd_playback *p, int32_t op, double extrabits,
                        record_list *records) {
  qa_q2_sizebuf *message = &p->wire.message;
  /* TS `MSG_ReadByte(message) | command.extrabits << 8`: an overread byte (-1) ORs to -1. */
  int64_t payload_length =
      (int64_t)(qa_q2_msg_read_byte(message) | (qa_js_to_int32(extrabits) << 8));
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
  /* data.slice(readcount, end): empty when end precedes readcount (negative length). */
  size_t payload_len = end > message->readcount ? (size_t)(end - message->readcount) : 0;
  uint8_t *copy = (uint8_t *)malloc(payload_len > 0 ? payload_len : 1);
  if (copy == NULL) return false;
  if (payload_len > 0) memcpy(copy, message->data + message->readcount, payload_len);
  message->readcount = end;
  bool ok = true;
  if (visible) {
    qa_cleanup node;
    qa_cleanup_push(&node, bytes_release, &copy);
    qa_q2_server_record *nested = NULL;
    size_t nested_count = 0;
    ok = qa_q2_server_message_reader_read(&p->embedded, copy, payload_len, &nested,
                                          &nested_count);
    qa_cleanup_pop(&node);
    size_t i = 0;
    for (; ok && i < nested_count; i++) ok = records_push(records, &nested[i]);
    /* Pushed records were zeroed; the rest (after an OOM) are released here. */
    qa_q2_server_records_free(nested, nested_count);
  }
  free(copy);
  return ok;
}

/* TS mvd_sound case. False only on OOM. */
static bool read_sound(qa_q2_mvd_playback *p, double extrabits, int64_t start,
                       record_list *records) {
  qa_q2_sizebuf *message = &p->wire.message;
  int32_t flags = qa_q2_msg_read_byte(message);
  double index = (flags & 32) ? (double)qa_q2_msg_read_word(message)
                              : (double)qa_q2_msg_read_byte(message);
  double volume = (flags & 1) ? (double)qa_q2_msg_read_byte(message) / 255 : 1;
  double attenuation = (flags & 2) ? (double)qa_q2_msg_read_byte(message) / 64 : 1;
  double delay = (flags & 16) ? (double)qa_q2_msg_read_byte(message) / 1000 : 0;
  int32_t channel_word = qa_q2_msg_read_word(message);
  double entity_number = (double)((uint32_t)channel_word >> 3);
  if (entity_number >= p->profile.max_entities) {
    qa_q2_throw_error("Invalid MVD sound entity");
  }
  const qa_q2_wire_entity *state = NULL;
  for (size_t i = 0; i < p->entity_count; i++) {
    if (p->entities[i].number == entity_number) state = &p->entities[i];
  }
  if (state == NULL) return true;
  qa_q2_wire_entity state_copy = *state;
  float position[3] = {0, 0, 0};
  p->visibility.sound_origin(p->visibility.ctx, &state_copy, position);
  qa_q2_wire_player player = current_player(p);
  if ((qa_js_to_int32(extrabits) & 1) == 0 &&
      !p->visibility.sound_audible(p->visibility.ctx, position, &player, p->portal_bits,
                                   p->portal_bits_length)) {
    return true;
  }
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
  return emit_record(p, records, 9, start, &event);
}

bool qa_q2_mvd_playback_read_records(qa_q2_mvd_playback *p, const uint8_t *bytes,
                                     size_t length, qa_q2_mvd_record_sink sink,
                                     void *ctx) {
  if (length < 1 || length > QA_Q2_MVD_MAX_MESSAGE) {
    qa_range_error("Invalid MVD message length");
  }
  if (!qa_q2_wire_codec_begin(&p->wire, bytes, length)) return false;
  qa_q2_sizebuf *message = &p->wire.message;
  record_list records = {NULL, 0, 0};
  qa_cleanup node;
  qa_cleanup_push(&node, record_list_release, &records);
  bool ok = true;
  while (ok && message->readcount < message->cursize) {
    int64_t start = message->readcount;
    qa_q2_mvd_cmd command;
    qa_q2_msg_read_mvd_cmd(message, &command);
    int32_t op = (int32_t)command.op;
    switch (op) {
      case QA_Q2_MVD_NOP:
        break;
      case QA_Q2_MVD_SERVERDATA:
        ok = read_serverdata(p, bytes, length, start, &records, sink, ctx);
        break;
      case QA_Q2_MVD_FRAME:
        if (!p->has_data) qa_q2_throw_error("MVD frame before serverdata");
        ok = read_frame(p, p->dummy) && emit_frame(p, &records, start);
        break;
      case QA_Q2_MVD_CONFIGSTRING: {
        double index = (double)qa_q2_msg_read_word(message);
        char value[2048];
        qa_q2_msg_read_string(message, value, sizeof value);
        if (index >= p->profile.max_config_strings) {
          qa_q2_throw_error("Invalid MVD configstring index");
        }
        ok = config_set(p, index, value);
        if (ok) {
          qa_q2_server_event event;
          memset(&event, 0, sizeof event);
          event.kind = QA_Q2_SERVER_CONFIG_STRING;
          event.index = index;
          ok = emit_text(p, &records, 13, start, &event, value);
        }
        break;
      }
      case QA_Q2_MVD_PRINT: {
        double level = (double)qa_q2_msg_read_byte(message);
        char text[2048];
        qa_q2_msg_read_string(message, text, sizeof text);
        qa_q2_server_event event;
        memset(&event, 0, sizeof event);
        event.kind = QA_Q2_SERVER_PRINT;
        event.level = level;
        ok = emit_text(p, &records, 10, start, &event, text);
        break;
      }
      case QA_Q2_MVD_SOUND:
        ok = read_sound(p, command.extrabits, start, &records);
        break;
      case QA_Q2_MVD_UNICAST: case QA_Q2_MVD_UNICAST_R:
      case QA_Q2_MVD_MULTICAST_ALL: case QA_Q2_MVD_MULTICAST_ALL_R:
      case QA_Q2_MVD_MULTICAST_PVS: case QA_Q2_MVD_MULTICAST_PVS_R:
      case QA_Q2_MVD_MULTICAST_PHS: case QA_Q2_MVD_MULTICAST_PHS_R:
        ok = read_routed(p, op, command.extrabits, &records);
        break;
      default:
        qa_q2_throw_error("Unsupported MVD command %s", QA_JS_NUMBER(command.op));
    }
    if (ok) qa_q2_message_check_read(message);
  }
  if (ok) qa_q2_wire_codec_finish(&p->wire);
  qa_cleanup_pop(&node);
  if (!ok) {
    record_list_release(&records);
    return false;
  }
  flush_records(&records, sink, ctx);
  return true;
}

typedef struct collect_state {
  record_list list; /* first: released by record_list_release */
  bool oom;
} collect_state;
static void collect_record(void *ctx, qa_q2_server_record *record) {
  collect_state *state = (collect_state *)ctx;
  if (!records_push(&state->list, record)) {
    qa_q2_server_event_clear(&record->event);
    free(record->raw);
    state->oom = true;
  }
}

bool qa_q2_mvd_playback_read(qa_q2_mvd_playback *p, const uint8_t *bytes,
                             size_t length, qa_q2_server_record **out,
                             size_t *out_count) {
  *out = NULL;
  *out_count = 0;
  collect_state collected = {{NULL, 0, 0}, false};
  qa_cleanup node;
  qa_cleanup_push(&node, record_list_release, &collected.list);
  bool ok = qa_q2_mvd_playback_read_records(p, bytes, length, collect_record, &collected);
  qa_cleanup_pop(&node);
  if (!ok || collected.oom) {
    record_list_release(&collected.list);
    return false;
  }
  *out = collected.list.items;
  *out_count = collected.list.count;
  return true;
}
