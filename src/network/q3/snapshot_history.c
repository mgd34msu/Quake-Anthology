/* Port of src/network/q3/snapshot-history.ts. GPL-2.0-or-later. */
#include "qa/network/q3/snapshot_history.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
bool qa_q3_copy_snapshot(qa_q3_wire_snapshot *dst, const qa_q3_wire_snapshot *src) {
  return qa_q3_wire_snapshot_copy(dst, src);
}
typedef struct { bool present; qa_q3_snapshot_history_entry entry; bool allocated; } slot_t;
struct qa_q3_snapshot_history {
  slot_t slots[QA_Q3_SNAPSHOT_BACKUP];
  bool has_current; qa_q3_wire_snapshot current;
  qa_q3_source_parse_entities *parse_entities;
};
static void zero_snapshot(qa_q3_product product, qa_q3_wire_snapshot *out) {
  memset(out, 0, sizeof *out);
  qa_q3_player_state_record_init(&out->player_state, product, 0, 0, 0);
  out->area_mask_len = 32; /* TS new Uint8Array(32) */
}
static void copy_into(qa_q3_wire_snapshot *dst, const qa_q3_wire_snapshot *src) {
  qa_q3_wire_snapshot_destroy(dst);
  if (!qa_q3_wire_snapshot_copy(dst, src)) qa_range_error("snapshot copy allocation failed");
  /* TS areaMask.fill(0) then set(source.areaMask) over the retained 32-byte buffer. */
  memset(dst->area_mask, 0, sizeof dst->area_mask);
  memcpy(dst->area_mask, src->area_mask, src->area_mask_len);
  dst->area_mask_len = sizeof dst->area_mask;
}
static void check_message_number(int64_t n) {
  if (n < -((int64_t)0x80000000) || n > 0x7fffffff) qa_range_error("Snapshot message number must be an int32 sequence");
}
bool qa_q3_snapshot_history_init(qa_q3_snapshot_history **out, qa_q3_source_parse_entities *parse_entities) {
  qa_q3_snapshot_history *h = calloc(1, sizeof *h);
  if (!h) return false;
  h->parse_entities = parse_entities;
  *out = h; return true;
}
void qa_q3_snapshot_history_destroy(qa_q3_snapshot_history *h) {
  if (!h) return;
  for (int i = 0; i < QA_Q3_SNAPSHOT_BACKUP; i++) qa_q3_wire_snapshot_destroy(&h->slots[i].entry.snapshot);
  qa_q3_wire_snapshot_destroy(&h->current);
  free(h);
}
qa_q3_source_parse_entities *qa_q3_snapshot_history_parse_entities(const qa_q3_snapshot_history *h) { return h->parse_entities; }
bool qa_q3_snapshot_history_latest(const qa_q3_snapshot_history *h, qa_q3_wire_snapshot *out) {
  if (!h->has_current) return false;
  return qa_q3_copy_snapshot(out, &h->current);
}
bool qa_q3_snapshot_history_read_current_player_state(const qa_q3_snapshot_history *h, qa_q3_player_state_record *out) {
  if (!h->has_current) return false;
  *out = h->current.player_state; return true;
}
bool qa_q3_snapshot_history_read_slot(const qa_q3_snapshot_history *h, int64_t message_number, bool *valid, qa_q3_wire_snapshot *out) {
  check_message_number(message_number);
  const slot_t *entry = &h->slots[((int32_t)message_number) & (QA_Q3_SNAPSHOT_BACKUP - 1)];
  if (!entry->allocated || !entry->present) return false;
  *valid = entry->entry.valid;
  return qa_q3_copy_snapshot(out, &entry->entry.snapshot);
}
qa_q3_snapshot_history_entry *qa_q3_snapshot_history_borrow_slot(qa_q3_snapshot_history *h, int64_t message_number, qa_q3_product product) {
  check_message_number(message_number);
  slot_t *entry = &h->slots[((int32_t)message_number) & (QA_Q3_SNAPSHOT_BACKUP - 1)];
  if (!entry->allocated) {
    entry->entry.valid = false;
    zero_snapshot(product, &entry->entry.snapshot);
    entry->present = false; entry->allocated = true;
  } else if (!entry->present && entry->entry.snapshot.player_state.product != product) {
    qa_q3_player_state_record_init(&entry->entry.snapshot.player_state, product, 0, 0, 0);
  }
  return &entry->entry;
}
bool qa_q3_snapshot_history_publish(qa_q3_snapshot_history *h, const qa_q3_server_operation *operation) {
  if (operation->validity.kind == QA_Q3_SNAP_INVALID) return false;
  const qa_q3_wire_snapshot *incoming = &operation->snapshot;
  check_message_number(incoming->message_number);
  int32_t previous_number = h->has_current ? (int32_t)h->current.message_number : 0;
  qa_q3_wire_snapshot snapshot;
  if (!qa_q3_copy_snapshot(&snapshot, incoming)) return false;
  int32_t msg = (int32_t)snapshot.message_number;
  int32_t skipped = (int32_t)((uint32_t)previous_number + 1u);
  if ((int32_t)((uint32_t)msg - (uint32_t)skipped) >= QA_Q3_SNAPSHOT_BACKUP)
    skipped = (int32_t)((uint32_t)msg - (QA_Q3_SNAPSHOT_BACKUP - 1));
  for (int cleared = 0; skipped < msg && cleared < QA_Q3_SNAPSHOT_BACKUP; skipped = (int32_t)((uint32_t)skipped + 1u), cleared++) {
    slot_t *entry = &h->slots[skipped & (QA_Q3_SNAPSHOT_BACKUP - 1)];
    if (entry->allocated) entry->entry.valid = false;
  }
  qa_q3_wire_snapshot_destroy(&h->current);
  h->current = snapshot; h->has_current = true;
  slot_t *entry = &h->slots[msg & (QA_Q3_SNAPSHOT_BACKUP - 1)];
  if (!entry->allocated) {
    zero_snapshot(snapshot.player_state.product, &entry->entry.snapshot);
    entry->allocated = true;
  }
  copy_into(&entry->entry.snapshot, &h->current);
  entry->entry.valid = true; entry->present = true;
  return true;
}
void qa_q3_snapshot_history_clear(qa_q3_snapshot_history *h) {
  for (int i = 0; i < QA_Q3_SNAPSHOT_BACKUP; i++) {
    slot_t *entry = &h->slots[i];
    if (!entry->allocated) continue;
    qa_q3_wire_snapshot zero; zero_snapshot(entry->entry.snapshot.player_state.product, &zero);
    copy_into(&entry->entry.snapshot, &zero);
    entry->entry.valid = false; entry->present = false;
  }
  qa_q3_wire_snapshot_destroy(&h->current);
  memset(&h->current, 0, sizeof h->current);
  h->has_current = false;
}
