/* Port of src/network/q3/state/player.ts. GPL-2.0-or-later. */
#include "qa/network/q3/state/player.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
bool qa_q3_player_state_slots_init(qa_q3_player_state_slots *s, int64_t length,
                                   const int32_t *source_values, int64_t source_length, bool has_source) {
  if (has_source && source_length != length)
    qa_range_error("Player state source slots require %lld values, got %lld",
                   (long long)length, (long long)source_length);
  if (length < 0) qa_range_error("Invalid typed array length: %lld", (long long)length);
  s->length = length;
  s->values = calloc((size_t)(length > 0 ? length : 1), sizeof(int32_t));
  if (!s->values) return false;
  if (has_source && source_values) memcpy(s->values, source_values, (size_t)length * sizeof(int32_t));
  return true;
}
void qa_q3_player_state_slots_destroy(qa_q3_player_state_slots *s) { free(s->values); s->values = NULL; s->length = 0; }
int32_t qa_q3_player_state_slots_get(const qa_q3_player_state_slots *s, int64_t index) {
  if (index < 0 || index >= s->length)
    qa_range_error("Player state slot %lld outside %lld", (long long)index, (long long)s->length);
  return s->values[index];
}
void qa_q3_player_state_slots_set(qa_q3_player_state_slots *s, int64_t index, int32_t value) {
  if (index < 0 || index >= s->length)
    qa_range_error("Player state slot %lld outside %lld", (long long)index, (long long)s->length);
  s->values[index] = value; /* Int32Array store truncates like TS */
}
bool qa_q3_player_state_slots_copy(const qa_q3_player_state_slots *s, int32_t **out) {
  *out = malloc((size_t)(s->length > 0 ? s->length : 1) * sizeof(int32_t));
  if (!*out) return false;
  memcpy(*out, s->values, (size_t)s->length * sizeof(int32_t));
  return true;
}
void qa_q3_player_state_record_init(qa_q3_player_state_record *s, qa_q3_product product,
                                    int32_t pm_type, int32_t weapon, int32_t weapon_state) {
  memset(s, 0, sizeof *s);
  s->product = product; s->pm_type = pm_type; s->weapon = weapon; s->weapon_state = weapon_state;
}
qa_q3_product qa_q3_player_state_record_get_product(const qa_q3_player_state_record *s) { return s->product; }
void qa_q3_player_state_record_copy_from(qa_q3_player_state_record *dst, const qa_q3_player_state_record *src) { *dst = *src; }
void qa_q3_player_state_record_copy(qa_q3_player_state_record *out, const qa_q3_player_state_record *s) {
  qa_q3_player_state_record_init(out, s->product, s->pm_type, s->weapon, s->weapon_state);
  *out = *s;
}
static int32_t *slot_array(qa_q3_player_state_record *s, int kind, int64_t *len) {
  switch (kind) {
    case 0: *len = 2; return s->events;
    case 1: *len = 2; return s->event_parms;
    case 2: *len = 16; return s->stats;
    case 3: *len = 16; return s->persistant;
    case 4: *len = 16; return s->powerups;
    default: *len = 16; return s->ammo;
  }
}
int32_t qa_q3_player_state_slot_get(const qa_q3_player_state_record *s, int kind, int64_t index) {
  qa_q3_player_state_record copy = *s; int64_t len; int32_t *arr = slot_array(&copy, kind, &len);
  if (index < 0 || index >= len) qa_range_error("Player state slot %lld outside %lld", (long long)index, (long long)len);
  return arr[index];
}
void qa_q3_player_state_slot_set(qa_q3_player_state_record *s, int kind, int64_t index, int32_t value) {
  int64_t len; int32_t *arr = slot_array(s, kind, &len);
  if (index < 0 || index >= len) qa_range_error("Player state slot %lld outside %lld", (long long)index, (long long)len);
  arr[index] = value;
}
