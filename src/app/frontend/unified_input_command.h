#ifndef QA_FRONTEND_UNIFIED_INPUT_COMMAND_H
#define QA_FRONTEND_UNIFIED_INPUT_COMMAND_H
#include "qa/input.h"
#include "qa/network_unified.h"

/* Unified widens the same qsrc physical command. Time metadata remains wide. */
typedef qa_input_command_builder frontend_unified_command_builder;
typedef qa_input_command_frame frontend_unified_command_frame;
bool frontend_unified_command_build(frontend_unified_command_builder *,
    const qa_input_command_tuning *, const qa_seat_input_sample *,
    const frontend_unified_command_frame *, double elapsed_ms,
    qa_unified_movement *, qa_error *);
#endif
