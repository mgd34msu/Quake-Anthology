#include "unified_input_command.h"

bool frontend_unified_command_build(frontend_unified_command_builder *builder,
    const qa_input_command_tuning *tuning, const qa_seat_input_sample *sample,
    const frontend_unified_command_frame *frame, double elapsed,
    qa_unified_movement *out, qa_error *error) {
    qa_input_command_builder next = *builder;
    qa_input_command_intent intent;
    if (!qa_input_command_sample(&next, tuning, sample, frame, elapsed, &intent, error)) return false;
    qa_usercmd source;
    qa_usercmd_build(&intent, frame, elapsed, &source);
    qa_unified_vec3 angles = {source.angles.x, source.angles.y, source.angles.z};
    qa_unified_movement command = {.kind = source.kind};
    switch (source.kind) {
    case QA_RULESET_NETQUAKE:
        command.data.nq.acknowledged_seconds = source.acknowledged_server_seconds;
        command.data.nq.angles = angles; command.data.nq.forward = source.forward_move;
        command.data.nq.side = source.side_move; command.data.nq.up = source.up_move;
        command.data.nq.buttons = source.buttons; command.data.nq.impulse = source.impulse; break;
    case QA_RULESET_QUAKEWORLD:
        command.data.qw.milliseconds = source.milliseconds; command.data.qw.angles = angles;
        command.data.qw.forward = source.forward_move; command.data.qw.side = source.side_move;
        command.data.qw.up = source.up_move; command.data.qw.buttons = source.buttons;
        command.data.qw.impulse = source.impulse; break;
    case QA_RULESET_Q2_CLASSIC:
        command.data.q2.milliseconds = source.milliseconds;
        for (unsigned i = 0; i < 3; ++i) command.data.q2.angle_shorts[i] = source.angle_words[i];
        command.data.q2.forward = source.forward_move; command.data.q2.side = source.side_move;
        command.data.q2.up = source.up_move; command.data.q2.buttons = source.buttons;
        command.data.q2.impulse = source.impulse; command.data.q2.light_level = source.light_level; break;
    case QA_RULESET_Q2_RERELEASE:
        command.data.q2r.milliseconds = source.milliseconds; command.data.q2r.angles = angles;
        command.data.q2r.forward = source.forward_move; command.data.q2r.side = source.side_move;
        command.data.q2r.buttons = source.buttons; command.data.q2r.server_frame = source.server_frame; break;
    case QA_RULESET_Q3:
        command.data.q3.server_time_ms = source.server_time_ms;
        for (unsigned i = 0; i < 3; ++i) command.data.q3.angle_words[i] = source.angle_words[i];
        command.data.q3.forward = source.forward_move; command.data.q3.right = source.side_move;
        command.data.q3.up = source.up_move; command.data.q3.buttons = source.buttons;
        command.data.q3.weapon = source.weapon; break;
    }
    *builder = next; *out = command; return true;
}
