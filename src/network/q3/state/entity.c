/* Port of src/network/q3/state/entity.ts. GPL-2.0-or-later. */
#include "qa/network/q3/state/entity.h"
#include <string.h>
void qa_q3_entity_state_record_init(qa_q3_entity_state_record *s, int32_t zero_type) {
  memset(s, 0, sizeof *s);
  qa_q3_wire_trajectory_init(&s->pos, zero_type);
  qa_q3_wire_trajectory_init(&s->apos, zero_type);
}
void qa_q3_entity_state_init(qa_q3_entity_state_record *s) { qa_q3_entity_state_record_init(s, 0); }
void qa_q3_copy_entity_state_fields(qa_q3_entity_state_record *dst, const qa_q3_entity_state_record *src) { *dst = *src; }
void qa_q3_entity_state_record_copy_from(qa_q3_entity_state_record *dst, const qa_q3_entity_state_record *src) { *dst = *src; }
void qa_q3_entity_state_record_copy(qa_q3_entity_state_record *out, const qa_q3_entity_state_record *s) {
  qa_q3_entity_state_record_init(out, s->pos.type);
  *out = *s;
}
