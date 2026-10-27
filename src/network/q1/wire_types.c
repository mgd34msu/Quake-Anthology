/* Port of src/network/q1/wire-types.ts. GPL-2.0-or-later. */
#include "qa/network/q1/wire_types.h"
#include "qa/foundation/support.h"
#include <string.h>
const int QA_Q1_AXES[3] = { 0, 1, 2 };
void qa_wire_vector_init(qa_wire_vector *vec) { vec->v[0] = vec->v[1] = vec->v[2] = 0.0f; }
double qa_wire_vector_get(const qa_wire_vector *vec, size_t index) { return (double)vec->v[index]; }
void qa_wire_vector_set(qa_wire_vector *vec, size_t index, double value) { vec->v[index] = (float)value; }
void qa_q1_sys_error(const char *message) { qa_range_error("%s", message); }
void qa_q1_wire_entity_state_init(qa_q1_wire_entity_state *s) {
  memset(s, 0, sizeof *s); s->alpha = 0; s->scale = 16;
}
void qa_q1_wire_entity_state_clear(qa_q1_wire_entity_state *s) { qa_q1_wire_entity_state_init(s); }
void qa_q1_wire_entity_state_copy_from(qa_q1_wire_entity_state *s, const qa_q1_wire_entity_state *src) { *s = *src; }
