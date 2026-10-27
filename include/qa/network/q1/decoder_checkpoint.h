/* Port of src/network/q1/decoder-checkpoint.ts. Captured form is a
 * qa_save_value tree built by the caller-side capture helpers below
 * (plain struct mirror); readers consume qa_save_reader. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q1_DECODER_CHECKPOINT_H
#define QA_NETWORK_Q1_DECODER_CHECKPOINT_H
#include "qa/network/q1/qw_constants.h"
#include "qa/network/q1/wire_types.h"
#include "qa/persistence/value.h"
typedef struct qa_captured_wire_entity {
  double origin[3], angles[3];
  double modelindex, frame, colormap, skin, alpha, scale, effects;
} qa_captured_wire_entity;
typedef struct qa_captured_qw_wire_entity {
  qa_captured_wire_entity base; double number, flags;
} qa_captured_qw_wire_entity;
qa_captured_wire_entity qa_capture_wire_entity_nq(const qa_q1_wire_entity_state *state);
qa_captured_wire_entity qa_capture_wire_entity_qw(const qa_qw_entity_state *state);
qa_captured_qw_wire_entity qa_capture_qw_wire_entity(const qa_qw_entity_state *state);
void qa_read_nq_wire_entity(const qa_save_reader *reader, qa_q1_wire_entity_state *out);
void qa_read_qw_wire_entity(const qa_save_reader *reader, qa_qw_entity_state *out);
#endif
