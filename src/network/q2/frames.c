/* Port of src/network/q2/frames.ts. GPL-2.0-or-later. */
#include "qa/network/q2/frames.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/constants.h"

static bool kind_is(const qa_q2_protocol_identity *p, const char *kind) {
  return p->kind != NULL && strcmp(p->kind, kind) == 0;
}

void qa_q2_wire_frame_free(qa_q2_wire_frame *frame) {
  free(frame->area_bits);
  if (frame->split_players != NULL) {
    for (size_t i = 0; i < frame->split_count; i++) free(frame->split_players[i].area_bits);
    free(frame->split_players);
  }
  free(frame->entities);
  memset(frame, 0, sizeof *frame);
}

bool qa_q2_wire_frame_clone(const qa_q2_wire_frame *in, qa_q2_wire_frame *out) {
  qa_q2_wire_frame result;
  memset(&result, 0, sizeof result);
  result = *in;
  result.area_bits = NULL;
  result.split_players = NULL;
  result.entities = NULL;
  if (in->area_bits_length > 0) {
    result.area_bits = (uint8_t *)malloc(in->area_bits_length);
    if (result.area_bits == NULL) return false;
    memcpy(result.area_bits, in->area_bits, in->area_bits_length);
  }
  if (in->entity_count > 0) {
    result.entities = (qa_q2_wire_entity *)malloc(in->entity_count * sizeof *result.entities);
    if (result.entities == NULL) {
      qa_q2_wire_frame_free(&result);
      return false;
    }
    memcpy(result.entities, in->entities, in->entity_count * sizeof *result.entities);
  }
  if (in->has_split) {
    result.split_players = (qa_q2_split_player *)calloc(
        in->split_count > 0 ? in->split_count : 1, sizeof *result.split_players);
    if (result.split_players == NULL) {
      qa_q2_wire_frame_free(&result);
      return false;
    }
    result.split_count = in->split_count;
    for (size_t i = 0; i < in->split_count; i++) {
      result.split_players[i] = in->split_players[i];
      result.split_players[i].area_bits = NULL;
      if (in->split_players[i].area_bits_length > 0) {
        result.split_players[i].area_bits =
            (uint8_t *)malloc(in->split_players[i].area_bits_length);
        if (result.split_players[i].area_bits == NULL) {
          qa_q2_wire_frame_free(&result);
          return false;
        }
        memcpy(result.split_players[i].area_bits, in->split_players[i].area_bits,
               in->split_players[i].area_bits_length);
      }
    }
  }
  *out = result;
  return true;
}

void qa_q2_baseline_list_free(qa_q2_baseline_list *list) {
  free(list->entries);
  memset(list, 0, sizeof *list);
}

bool qa_q2_baseline_list_set(qa_q2_baseline_list *list, double number,
                             const qa_q2_wire_entity *state) {
  for (size_t i = 0; i < list->count; i++) {
    if (list->entries[i].number == number) {
      list->entries[i].state = *state;
      return true;
    }
  }
  if (list->count == list->capacity) {
    size_t next = list->capacity > 0 ? list->capacity * 2 : 16;
    qa_q2_baseline_entry *grown =
        (qa_q2_baseline_entry *)realloc(list->entries, next * sizeof *grown);
    if (grown == NULL) return false;
    list->entries = grown;
    list->capacity = next;
  }
  list->entries[list->count].number = number;
  list->entries[list->count].state = *state;
  list->count++;
  return true;
}

const qa_q2_wire_entity *qa_q2_baseline_list_get(const qa_q2_baseline_list *list,
                                                 double number) {
  for (size_t i = 0; i < list->count; i++) {
    if (list->entries[i].number == number) return &list->entries[i].state;
  }
  return NULL;
}

static void ordered(const qa_q2_wire_entity *entities, size_t count) {
  double previous = 0;
  for (size_t i = 0; i < count; i++) {
    if (entities[i].number <= previous || entities[i].number > 65535) {
      qa_range_error("Q2 frame entity numbers must be unique and sorted");
    }
    previous = entities[i].number;
  }
}

void qa_q2_write_packet_entities(qa_q2_protocol_codec *codec, qa_q2_sizebuf *message,
                                 const qa_q2_wire_entity *old_entities,
                                 size_t old_count,
                                 const qa_q2_wire_entity *current, size_t current_count,
                                 const qa_q2_baseline_list *baselines,
                                 double max_clients) {
  ordered(old_entities, old_count);
  ordered(current, current_count);
  codec->write_packet_entities_begin(codec, message);
  size_t before = 0;
  size_t after = 0;
  while (before < old_count || after < current_count) {
    const qa_q2_wire_entity *from = before < old_count ? &old_entities[before] : NULL;
    const qa_q2_wire_entity *to = after < current_count ? &current[after] : NULL;
    double old_number = from != NULL ? from->number : INFINITY;
    double new_number = to != NULL ? to->number : INFINITY;
    if (from != NULL && to != NULL && old_number == new_number) {
      codec->write_delta_entity(codec, message, from, to, false, to->number <= max_clients);
      before++;
      after++;
    } else if (to != NULL && new_number < old_number) {
      const qa_q2_wire_entity *baseline = qa_q2_baseline_list_get(baselines, to->number);
      qa_q2_wire_entity empty;
      qa_q2_wire_entity_init(&empty);
      codec->write_delta_entity(codec, message, baseline != NULL ? baseline : &empty, to,
                                true, true);
      after++;
    } else if (from != NULL) {
      codec->write_entity_remove(codec, message, from->number);
      before++;
    } else {
      qa_q2_throw_error("Invalid Q2 packet entity merge");
    }
  }
  codec->write_packet_entities_end(codec, message);
}

typedef struct entities_ctx {
  qa_q2_protocol_codec *codec;
  const qa_q2_wire_frame *frame;
  const qa_q2_wire_frame *old;
  const qa_q2_baseline_list *baselines;
  double max_clients;
} entities_ctx;

static void write_entities_cb(void *vctx, qa_q2_sizebuf *m) {
  entities_ctx *c = (entities_ctx *)vctx;
  qa_q2_write_packet_entities(c->codec, m,
                              c->old != NULL ? c->old->entities : NULL,
                              c->old != NULL ? c->old->entity_count : 0,
                              c->frame->entities, c->frame->entity_count,
                              c->baselines, c->max_clients);
}

uint8_t *qa_q2_encode_frame(qa_q2_wire_codec *wire, const qa_q2_wire_frame *frame,
                            const qa_q2_wire_frame *old_or_null,
                            const qa_q2_baseline_list *baselines, double max_clients,
                            size_t *out_length) {
  qa_q2_protocol_codec *codec = wire->selected_codec;
  qa_q2_sizebuf message;
  if (!qa_q2_message_create(&message, 32768)) return NULL;
  bool kex = kind_is(&wire->selected_protocol, "q2-kex") ||
             kind_is(&wire->selected_protocol, "q2-kex-demo");
  if (kex && frame->has_split) {
    qa_q2_msg_write_byte(&message, 20);
    qa_q2_msg_write_long(&message, frame->server_frame);
    qa_q2_msg_write_long(&message, old_or_null != NULL ? old_or_null->server_frame : -1);
    qa_q2_msg_write_byte(&message, frame->suppressed_count);
    size_t total = 1 + frame->split_count;
    for (size_t index = 0; index < total; index++) {
      const qa_q2_split_player *state = NULL;
      qa_q2_split_player head;
      memset(&head, 0, sizeof head);
      if (index == 0) {
        head.area_bits = frame->area_bits;
        head.area_bits_length = frame->area_bits_length;
        head.player = frame->player;
        state = &head;
      } else {
        state = &frame->split_players[index - 1];
      }
      const qa_q2_wire_player *previous = NULL;
      qa_q2_wire_player prev_storage;
      if (old_or_null != NULL) {
        if (index == 0) previous = &old_or_null->player;
        else if (old_or_null->has_split && index - 1 < old_or_null->split_count) {
          previous = &old_or_null->split_players[index - 1].player;
        }
      }
      if (previous == NULL) {
        qa_q2_wire_player_init(&prev_storage);
        previous = &prev_storage;
      }
      qa_q2_msg_write_byte(&message, (double)state->area_bits_length);
      qa_q2_sz_write(&message, state->area_bits, state->area_bits_length,
                     (int64_t)state->area_bits_length);
      qa_q2_msg_write_byte(&message, 17);
      codec->write_player_state_delta(codec, &message, previous, &state->player);
    }
    qa_q2_write_packet_entities(
        codec, &message, old_or_null != NULL ? old_or_null->entities : NULL,
        old_or_null != NULL ? old_or_null->entity_count : 0, frame->entities,
        frame->entity_count, baselines, max_clients);
    uint8_t *out = qa_q2_message_bytes(&message, out_length);
    qa_q2_message_destroy(&message);
    return out;
  }
  qa_q2_frame_write_params params;
  memset(&params, 0, sizeof params);
  params.framenum = frame->server_frame;
  params.lastframe = old_or_null != NULL ? old_or_null->server_frame : -1;
  params.surpress_count = frame->suppressed_count;
  params.areabits = frame->area_bits;
  params.areabits_length = frame->area_bits_length;
  params.areabytes = (double)frame->area_bits_length;
  params.ps_from = old_or_null != NULL ? &old_or_null->player : NULL;
  params.ps_to = &frame->player;
  entities_ctx c = {codec, frame, old_or_null, baselines, max_clients};
  codec->write_frame(codec, &message, &params, write_entities_cb, &c);
  uint8_t *out = qa_q2_message_bytes(&message, out_length);
  qa_q2_message_destroy(&message);
  return out;
}

bool qa_q2_frame_history_init(qa_q2_frame_history *history, double capacity) {
  memset(history, 0, sizeof *history);
  double cap = capacity == 0 ? 16 : capacity;
  if (cap != trunc(cap) || cap < 1) qa_range_error("Invalid Q2 frame history");
  history->capacity = cap;
  return true;
}

void qa_q2_frame_history_destroy(qa_q2_frame_history *history) {
  if (history->frames != NULL) {
    for (size_t i = 0; i < history->frame_count; i++) qa_q2_wire_frame_free(&history->frames[i]);
    free(history->frames);
  }
  qa_q2_baseline_list_free(&history->baselines);
  memset(history, 0, sizeof *history);
}

qa_q2_wire_frame *qa_q2_frame_history_get(qa_q2_frame_history *history, double number) {
  for (size_t i = 0; i < history->frame_count; i++) {
    if (history->frames[i].server_frame == number) return &history->frames[i];
  }
  return NULL;
}

qa_q2_wire_frame *qa_q2_frame_history_latest(qa_q2_frame_history *history) {
  qa_q2_wire_frame *latest = NULL;
  for (size_t i = 0; i < history->frame_count; i++) {
    qa_q2_wire_frame *frame = &history->frames[i];
    bool valid = !(frame->has_valid && !frame->valid);
    if (valid && (latest == NULL || frame->server_frame > latest->server_frame)) {
      latest = frame;
    }
  }
  return latest;
}

void qa_q2_frame_history_clear(qa_q2_frame_history *history) {
  for (size_t i = 0; i < history->frame_count; i++) qa_q2_wire_frame_free(&history->frames[i]);
  history->frame_count = 0;
  free(history->baselines.entries);
  history->baselines.entries = NULL;
  history->baselines.count = 0;
  history->baselines.capacity = 0;
}

bool qa_q2_frame_history_accept(qa_q2_frame_history *history,
                                const qa_q2_wire_frame *frame) {
  qa_q2_wire_frame clone;
  if (!qa_q2_wire_frame_clone(frame, &clone)) return false;
  long at = -1;
  for (size_t i = 0; i < history->frame_count; i++) {
    if (history->frames[i].server_frame == frame->server_frame) at = (long)i;
  }
  if (at >= 0) {
    qa_q2_wire_frame_free(&history->frames[at]);
    history->frames[at] = clone;
  } else {
    if (history->frame_count == history->frame_capacity) {
      size_t next = history->frame_capacity > 0 ? history->frame_capacity * 2 : 8;
      qa_q2_wire_frame *grown =
          (qa_q2_wire_frame *)realloc(history->frames, next * sizeof *grown);
      if (grown == NULL) {
        qa_q2_wire_frame_free(&clone);
        return false;
      }
      history->frames = grown;
      history->frame_capacity = next;
    }
    history->frames[history->frame_count++] = clone;
  }
  while ((double)history->frame_count > history->capacity) {
    if (history->frame_count == 0) break;
    qa_q2_wire_frame_free(&history->frames[0]);
    memmove(&history->frames[0], &history->frames[1],
            (history->frame_count - 1) * sizeof(qa_q2_wire_frame));
    history->frame_count--;
  }
  return true;
}

typedef struct entity_list {
  qa_q2_wire_entity *items;
  size_t count, capacity;
} entity_list;

static bool entity_push(entity_list *list, qa_q2_wire_entity entity) {
  if (list->count == list->capacity) {
    size_t next = list->capacity > 0 ? list->capacity * 2 : 16;
    qa_q2_wire_entity *grown =
        (qa_q2_wire_entity *)realloc(list->items, next * sizeof *grown);
    if (grown == NULL) return false;
    list->items = grown;
    list->capacity = next;
  }
  list->items[list->count++] = entity;
  return true;
}

qa_q2_wire_frame *qa_q2_frame_history_read(qa_q2_frame_history *history,
                                           qa_q2_wire_codec *wire,
                                           bool read_suppress_byte) {
  qa_q2_protocol_codec *codec = wire->selected_codec;
  uint8_t area_storage[255];
  memset(area_storage, 0, sizeof area_storage);
  qa_q2_frame_header header;
  codec->read_frame_header(codec, area_storage, sizeof area_storage, read_suppress_byte,
                           &header);
  const qa_q2_wire_frame *old =
      header.deltaframe > 0 ? qa_q2_frame_history_get(history, header.deltaframe) : NULL;
  bool old_invalid = old != NULL && old->has_valid && !old->valid;
  bool valid = header.deltaframe <= 0 || (old != NULL && !old_invalid);
  qa_q2_wire_player empty_player;
  qa_q2_wire_player_init(&empty_player);
  qa_q2_wire_frame frame;
  memset(&frame, 0, sizeof frame);
  qa_q2_wire_player_init(&frame.player);
  codec->read_frame_playerstate(codec, old != NULL ? &old->player : &empty_player,
                                &frame.player);
  qa_q2_split_player *splits = NULL;
  size_t split_count = 0;
  bool oom = false;
  if (kind_is(&wire->selected_protocol, "q2-kex") ||
      kind_is(&wire->selected_protocol, "q2-kex-demo")) {
    double total = qa_q2_kex_split_player_count(&wire->kex);
    size_t split_capacity = total > 1 ? (size_t)(total - 1) : 0;
    if (split_capacity > 0) {
      splits = (qa_q2_split_player *)calloc(split_capacity, sizeof *splits);
      if (splits == NULL) oom = true;
    }
    for (double index = 1; index < total && !oom; index++) {
      int32_t length = qa_q2_msg_read_byte(&wire->message);
      size_t len = length > 0 ? (size_t)length : 0;
      uint8_t *area = (uint8_t *)calloc(len > 0 ? len : 1, 1);
      if (area == NULL) { oom = true; break; }
      qa_q2_msg_read_data(&wire->message, area, len, length);
      qa_q2_wire_player next;
      qa_q2_wire_player_init(&next);
      const qa_q2_wire_player *prev = &empty_player;
      if (old != NULL && old->has_split && (size_t)(index - 1) < old->split_count) {
        prev = &old->split_players[(size_t)(index - 1)].player;
      }
      codec->read_frame_playerstate(codec, prev, &next);
      splits[split_count].area_bits = area;
      splits[split_count].area_bits_length = len;
      splits[split_count].player = next;
      split_count++;
    }
  }
  codec->read_packet_entities_begin(codec);
  entity_list entities = {NULL, 0, 0};
  const qa_q2_wire_entity *previous = old != NULL ? old->entities : NULL;
  size_t previous_count = old != NULL ? old->entity_count : 0;
  size_t cursor = 0;
  double last_number = 0;
  if (!oom) {
    for (;;) {
      qa_q2_entity_bits entity_header;
      codec->read_entity_bits(codec, &entity_header);
      qa_q2_message_check_read(&wire->message);
      if (entity_header.number == 0) break;
      if (entity_header.number <= last_number) qa_q2_throw_error("Unordered Q2 entity delta");
      last_number = entity_header.number;
      for (;;) {
        const qa_q2_wire_entity *from = cursor < previous_count ? &previous[cursor] : NULL;
        if (from == NULL || from->number >= entity_header.number) break;
        qa_q2_wire_entity entity;
        qa_q2_wire_entity_init(&entity);
        codec->read_delta_entity(codec, from, &entity, from->number, 0);
        if (!entity_push(&entities, entity)) { oom = true; break; }
        cursor++;
      }
      if (oom) break;
      const qa_q2_wire_entity *from = cursor < previous_count ? &previous[cursor] : NULL;
      if ((entity_header.bits_u64 & QA_Q2_U_REMOVE) != 0) {
        if (from == NULL || from->number != entity_header.number) {
          if (valid) qa_q2_throw_error("Q2 entity removal has no delta source");
        } else {
          cursor++;
        }
        continue;
      }
      bool matched = from != NULL && from->number == entity_header.number;
      const qa_q2_wire_entity *baseline =
          matched ? from : qa_q2_baseline_list_get(&history->baselines, entity_header.number);
      qa_q2_wire_entity empty_entity;
      qa_q2_wire_entity_init(&empty_entity);
      if (baseline == NULL) baseline = &empty_entity;
      qa_q2_wire_entity entity;
      qa_q2_wire_entity_init(&entity);
      codec->read_delta_entity(codec, baseline, &entity, entity_header.number,
                               entity_header.bits_u64);
      if (!entity_push(&entities, entity)) { oom = true; break; }
      if (matched) cursor++;
    }
  }
  if (!oom) {
    for (; cursor < previous_count; cursor++) {
      const qa_q2_wire_entity *from = &previous[cursor];
      qa_q2_wire_entity entity;
      qa_q2_wire_entity_init(&entity);
      codec->read_delta_entity(codec, from, &entity, from->number, 0);
      if (!entity_push(&entities, entity)) { oom = true; break; }
    }
  }
  if (oom) {
    free(entities.items);
    for (size_t i = 0; i < split_count; i++) free(splits[i].area_bits);
    free(splits);
    return NULL;
  }
  qa_q2_message_check_read(&wire->message);
  frame.has_valid = true;
  frame.valid = valid;
  frame.server_frame = header.serverframe;
  frame.delta_frame = header.deltaframe;
  frame.suppressed_count = header.surpress_count;
  size_t area_len = header.areabytes > 0 ? (size_t)header.areabytes : 0;
  if (area_len > sizeof area_storage) area_len = sizeof area_storage;
  frame.area_bits = (uint8_t *)malloc(area_len > 0 ? area_len : 1);
  if (frame.area_bits == NULL) {
    free(entities.items);
    for (size_t i = 0; i < split_count; i++) free(splits[i].area_bits);
    free(splits);
    return NULL;
  }
  if (area_len > 0) memcpy(frame.area_bits, area_storage, area_len);
  frame.area_bits_length = area_len;
  frame.entities = entities.items;
  frame.entity_count = entities.count;
  if (split_count > 0) {
    frame.has_split = true;
    frame.split_count = split_count;
    frame.split_players = splits;
    splits = NULL;
  }
  free(splits);
  if (!qa_q2_frame_history_accept(history, &frame)) {
    qa_q2_wire_frame_free(&frame);
    return NULL;
  }
  qa_q2_wire_frame *stored = qa_q2_frame_history_get(history, frame.server_frame);
  qa_q2_wire_frame_free(&frame);
  return stored;
}
