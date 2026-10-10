#include "unified_input_command.h"

bool frontend_unified_command_build(frontend_unified_command_builder *builder,
    const qa_input_command_tuning *tuning, const qa_seat_input_sample *sample,
    const frontend_unified_command_frame *frame, double elapsed,
    qa_usercmd *out, qa_error *error) {
    qa_input_command_builder next = *builder;
    qa_input_command_intent intent;
    if (!qa_input_command_sample(&next, tuning, sample, frame, elapsed, &intent, error)) return false;
    qa_usercmd_build(&intent, frame, elapsed, out);
    *builder = next; return true;
}
