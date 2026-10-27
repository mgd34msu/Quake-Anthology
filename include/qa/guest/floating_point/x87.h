/*
 * Port of src/guest/floating-point/x87.ts: x87 register stack and ESC
 * instruction execution. TS DeferredX87Exception/UnsupportedX87 throws ride
 * the numeric context's throw channel (see binary.h). Public register
 * helpers run under a temporary handler that swallows the deferred throw
 * exactly like executeX87 does. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_FLOATING_POINT_X87_H
#define QA_GUEST_FLOATING_POINT_X87_H

#include "qa/guest/floating_point/binary.h"

typedef enum qa_x87_storage { QA_X87_FLOAT32 = 0, QA_X87_FLOAT64 } qa_x87_storage;

void qa_read_x87_register(qa_binary_value *out, qa_guest_x87_state *state, int index);
void qa_write_x87_register(qa_guest_x87_state *state, int index, const qa_binary_value *value);
void qa_push_x87(qa_guest_x87_state *state, const qa_binary_value *value);
void qa_pop_x87(qa_guest_x87_state *state);
void qa_initialize_x87(qa_guest_x87_state *state);
void qa_write_x87_return(qa_guest_x87_state *state, double value, qa_x87_storage storage);
double qa_read_x87_return(qa_guest_x87_state *state, qa_x87_storage storage);
void qa_execute_x87(qa_numeric_result *out, qa_numeric_context *ctx);

#endif
