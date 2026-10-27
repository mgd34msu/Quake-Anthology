/* Port of src/compat/q2/native-input.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_NATIVE_INPUT_H
#define QA_COMPAT_Q2_NATIVE_INPUT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/math.h"
#include "qa/contracts/protocol.h"

typedef struct qa_compat_q2_native_input_motion_outputs { bool grounded; bool has_ground_slot; double ground_slot; bool ducked; double water_level, water_type, view_height; qa_vec3 view_angles; qa_bounds bounds; const double *touches; size_t touch_count; } qa_compat_q2_native_input_motion_outputs;
qa_q2_user_command qa_compat_q2_read_classic_user_command(const uint8_t *view);
typedef struct qa_compat_q2_native_input_binding { void *host_ctx; bool closed; } qa_compat_q2_native_input_binding;
void qa_compat_q2_native_input_binding_close(qa_compat_q2_native_input_binding *s);
#endif
