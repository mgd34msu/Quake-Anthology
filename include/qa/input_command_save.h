#ifndef QA_INPUT_COMMAND_SAVE_H
#define QA_INPUT_COMMAND_SAVE_H
#include "qa/input.h"
/* Exact retained builder scalars, including source-reachable drift infinity.
 * No command sampling/building, centering, callback or timing advancement. */
bool qa_input_command_checkpoint(const qa_input_command_builder *, qa_buffer *, qa_error *);
bool qa_input_command_restore(qa_input_command_builder *, qa_bytes, qa_error *);
#endif
