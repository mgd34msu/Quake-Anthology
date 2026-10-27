/* Port of src/network/q1/wire-types.ts. WireVector stores float so C
 * assignment reproduces the TS Math.fround setters exactly; reads widen
 * to double like TS numbers. Sys_Error -> qa_range_error. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_WIRE_TYPES_H
#define QA_NETWORK_Q1_WIRE_TYPES_H
#include <stddef.h>
typedef struct qa_wire_vector { float v[3]; } qa_wire_vector;
void qa_wire_vector_init(qa_wire_vector *vec);
double qa_wire_vector_get(const qa_wire_vector *vec, size_t index);
void qa_wire_vector_set(qa_wire_vector *vec, size_t index, double value);
extern const int QA_Q1_AXES[3];
#define QA_Q1_MAX_MODELS 8192
#define QA_Q1_MAX_MSGLEN 1450
#define QA_Q1_MAX_DATAGRAM 1450
void qa_q1_sys_error(const char *message);
typedef struct qa_q1_wire_entity_state {
  qa_wire_vector origin, angles;
  double modelindex, frame, colormap, skin, alpha, scale, effects;
} qa_q1_wire_entity_state;
void qa_q1_wire_entity_state_init(qa_q1_wire_entity_state *s);
void qa_q1_wire_entity_state_clear(qa_q1_wire_entity_state *s);
void qa_q1_wire_entity_state_copy_from(qa_q1_wire_entity_state *s, const qa_q1_wire_entity_state *src);
#endif
