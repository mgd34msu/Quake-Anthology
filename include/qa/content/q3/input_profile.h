/* Port of src/content/q3/input-profile.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_INPUT_PROFILE_H
#define QA_CONTENT_Q3_INPUT_PROFILE_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/equipment/lrctf_grapple_profile.h"
/* Mirror of QvmInputDefinition (TS owner: src/compat/qvm/game-input.ts). */
typedef struct qa_q3_qvm_input_definition {
  qa_module_identity module;
  double entity_stride, client_stride, client_pointer;
  double movement_normal, movement_noclip, movement_freeze;
  const double *intermission; size_t intermission_count;
  double entry_client_think, entry_run_client, entry_client_spawn, entry_move, entry_slice;
} qa_q3_qvm_input_definition;
bool qa_q3_input_profile(const qa_q3_equipment_qvm_artifact *artifact, qa_q3_qvm_input_definition *out);
#endif
