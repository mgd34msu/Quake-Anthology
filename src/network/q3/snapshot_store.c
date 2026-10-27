/* Port of src/network/q3/snapshot-store.ts. GPL-2.0-or-later. */
#include "qa/network/q3/snapshot_store.h"
#include "qa/core/common_error.h"
#include "qa/foundation/support.h"
#include "qa/network/q3/state_delta.h"
#include <stdlib.h>
#include <string.h>
struct qa_q3_snapshot_entities {
  qa_q3_entity_state_record *cells; bool *present;
  int64_t length; int64_t next;
};
bool qa_q3_snapshot_entities_init(qa_q3_snapshot_entities **out, int64_t length) {
  if (length < 1 || length > 131072) qa_range_error("Q3 snapshot entity storage outside 1..131072");
  qa_q3_snapshot_entities *e = calloc(1, sizeof *e);
  if (!e) return false;
  e->cells = calloc((size_t)length, sizeof *e->cells);
  e->present = calloc((size_t)length, sizeof *e->present);
  if (!e->cells || !e->present) { free(e->cells); free(e->present); free(e); return false; }
  e->length = length;
  *out = e; return true;
}
void qa_q3_snapshot_entities_destroy(qa_q3_snapshot_entities *e) { if (!e) return; free(e->cells); free(e->present); free(e); }
int64_t qa_q3_snapshot_entities_length(const qa_q3_snapshot_entities *e) { return e->length; }
int64_t qa_q3_snapshot_entities_next(const qa_q3_snapshot_entities *e) { return e->next; }
void qa_q3_snapshot_entities_set_next(qa_q3_snapshot_entities *e, int64_t next) { e->next = next; }
void qa_q3_snapshot_entities_append(qa_q3_snapshot_entities *e, const qa_q3_entity_state_record *entity) {
  int64_t index = e->next % e->length;
  if (index < 0) qa_range_error("Invalid snapshot entity slot");
  e->cells[index] = *entity; e->present[index] = true; e->next++;
  if (e->next >= 0x7ffffffe) qa_common_error(QA_COMMON_ERROR_FATAL, "svs.nextSnapshotEntities wrapped");
}
void qa_q3_snapshot_entities_read(const qa_q3_snapshot_entities *e, int64_t absolute, qa_q3_entity_state_record *out) {
  int64_t index = absolute % e->length;
  if (index < 0) qa_range_error("Invalid snapshot entity position");
  if (!e->present[index]) qa_q3_entity_state_record_init(out, 0);
  else *out = e->cells[index];
}
struct qa_q3_server_snapshot_history {
  qa_q3_server_frame frames[32];
  qa_q3_snapshot_entities *entities;
  qa_q3_product product;
  qa_q3_server_baseline_fn baseline; void *baseline_ctx;
};
bool qa_q3_server_snapshot_history_init(qa_q3_server_snapshot_history **out, qa_q3_snapshot_entities *entities,
                                        qa_q3_product product, qa_q3_server_baseline_fn baseline, void *baseline_ctx) {
  qa_q3_server_snapshot_history *h = calloc(1, sizeof *h);
  if (!h) return false;
  for (int i = 0; i < 32; i++) qa_q3_player_state_record_init(&h->frames[i].player_state, product, 0, 0, 0);
  h->entities = entities; h->product = product; h->baseline = baseline; h->baseline_ctx = baseline_ctx;
  *out = h; return true;
}
void qa_q3_server_snapshot_history_destroy(qa_q3_server_snapshot_history *h) {
  if (!h) return;
  for (int i = 0; i < 32; i++) free(h->frames[i].area_mask);
  free(h);
}
qa_q3_server_frame *qa_q3_server_snapshot_history_frames(qa_q3_server_snapshot_history *h, size_t *count) { *count = 32; return h->frames; }
qa_q3_server_frame *qa_q3_server_snapshot_history_frame(qa_q3_server_snapshot_history *h, int64_t sequence) {
  return &h->frames[((int32_t)sequence) & 31];
}
qa_q3_server_frame *qa_q3_server_snapshot_history_capture(qa_q3_server_snapshot_history *h, int64_t sequence,
                                                          const qa_q3_player_state_record *player,
                                                          const uint8_t *area_mask, size_t area_mask_len,
                                                          const qa_q3_entity_state_record *entities, size_t entity_count) {
  if (area_mask_len > 32 || entity_count > 256) qa_range_error("Q3 snapshot exceeds source area/entity capacities");
  qa_q3_server_frame *frame = qa_q3_server_snapshot_history_frame(h, sequence);
  frame->player_state = *player;
  free(frame->area_mask);
  frame->area_mask = malloc(area_mask_len ? area_mask_len : 1);
  if (!frame->area_mask) qa_range_error("snapshot frame allocation failed");
  memcpy(frame->area_mask, area_mask, area_mask_len);
  frame->area_mask_len = area_mask_len;
  frame->first_entity = h->entities->next; frame->num_entities = 0;
  for (size_t i = 0; i < entity_count; i++) { qa_q3_snapshot_entities_append(h->entities, &entities[i]); frame->num_entities++; }
  return frame;
}
int64_t qa_q3_server_snapshot_history_delta(const qa_q3_server_snapshot_history *h, int64_t sequence, int64_t requested, bool active) {
  if (requested <= 0 || !active || sequence - requested >= 29) return -1;
  const qa_q3_server_frame *old = &h->frames[((int32_t)requested) & 31];
  return old->first_entity <= h->entities->next - h->entities->length ? -1 : requested;
}
void qa_q3_server_snapshot_history_write(qa_q3_server_snapshot_history *h, qa_q3_message_writer *writer,
                                         int64_t sequence, int64_t requested, bool active, int64_t server_time, int64_t flags) {
  qa_q3_server_frame *current = qa_q3_server_snapshot_history_frame(h, sequence);
  int64_t delta = qa_q3_server_snapshot_history_delta(h, sequence, requested, active);
  qa_q3_server_frame *old = delta < 0 ? NULL : qa_q3_server_snapshot_history_frame(h, delta);
  qa_q3_message_writer_write_byte(writer, QA_Q3_SVC_SNAPSHOT);
  qa_q3_message_writer_write_long(writer, server_time);
  qa_q3_message_writer_write_byte(writer, delta < 0 ? 0 : sequence - delta);
  qa_q3_message_writer_write_byte(writer, flags);
  qa_q3_message_writer_write_byte(writer, (int64_t)current->area_mask_len);
  qa_q3_message_writer_write_data(writer, current->area_mask, current->area_mask_len);
  qa_q3_write_delta_player_state(writer, old ? &old->player_state : NULL, &current->player_state);
  int64_t new_index = 0, old_index = 0;
  while (new_index < current->num_entities || (old && old_index < old->num_entities)) {
    bool has_next = new_index < current->num_entities;
    bool has_prev = old && old_index < old->num_entities;
    qa_q3_entity_state_record next, previous;
    if (has_next) qa_q3_snapshot_entities_read(h->entities, current->first_entity + new_index, &next);
    if (has_prev) qa_q3_snapshot_entities_read(h->entities, old->first_entity + old_index, &previous);
    if (has_next && has_prev && next.number == previous.number) {
      qa_q3_write_delta_entity(writer, &previous, &next, false); new_index++; old_index++;
    } else if (has_next && (!has_prev || next.number < previous.number)) {
      qa_q3_entity_state_record base;
      h->baseline(h->baseline_ctx, next.number, &base);
      qa_q3_write_delta_entity(writer, &base, &next, true); new_index++;
    } else if (has_prev) { qa_q3_write_delta_entity(writer, &previous, NULL, true); old_index++; }
    else qa_range_error("Missing snapshot merge entity");
  }
  qa_q3_message_writer_write_bits(writer, 1023, 10);
}
bool qa_q3_server_snapshot_history_copy(qa_q3_server_snapshot_history *h, int64_t sequence, int64_t delta_number,
                                        int64_t server_time, int64_t flags, int64_t server_command_number, qa_q3_wire_snapshot *out) {
  qa_q3_server_frame *frame = qa_q3_server_snapshot_history_frame(h, sequence);
  memset(out, 0, sizeof *out);
  out->entities = malloc((frame->num_entities ? (size_t)frame->num_entities : 1) * sizeof *out->entities);
  if (!out->entities) return false;
  for (int64_t i = 0; i < frame->num_entities; i++)
    qa_q3_snapshot_entities_read(h->entities, frame->first_entity + i, &out->entities[i]);
  out->entity_count = (size_t)frame->num_entities;
  out->message_number = sequence; out->delta_number = delta_number; out->server_time = server_time;
  out->flags = flags; out->server_command_number = server_command_number;
  out->parse_entities_number = frame->first_entity;
  memcpy(out->area_mask, frame->area_mask, frame->area_mask_len);
  out->area_mask_len = frame->area_mask_len;
  out->player_state = frame->player_state;
  return true;
}

qa_q3_product qa_q3_server_snapshot_history_get_product(const qa_q3_server_snapshot_history *h) { return h->product; }
qa_q3_snapshot_entities *qa_q3_server_snapshot_history_get_entities(const qa_q3_server_snapshot_history *h) { return h->entities; }
void qa_q3_server_snapshot_history_baseline(const qa_q3_server_snapshot_history *h, int64_t number, qa_q3_entity_state_record *out) {
  h->baseline(h->baseline_ctx, number, out);
}
