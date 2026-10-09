#include "qa/input.h"
#include "qa/text.h"
#include <string.h>

static const struct { float move, walk, source_speed; } stock[] = {
    {320, 320, 400}, {320, 320, 400}, {400, 400, 400},
    {400, 400, 400}, {127, 64, 400}
};
#define PHYSICAL_MOVE_UNIT 512.0f

static float clamp(float value, float low, float high) { return fmaxf(low, fminf(high, value)); }
static float fraction(const qa_seat_input_sample *s, qa_input_action action) {
    return s->buttons[action].fraction;
}
static bool active(const qa_seat_input_sample *s, qa_input_action action) {
    return s->buttons[action].active;
}
static bool pressed(const qa_seat_input_sample *s, qa_input_action action) {
    return active(s, action) || s->buttons[action].pressed;
}
static float add(float value, float amount, bool integral) {
    return integral ? truncf(value + amount) : value + amount;
}
qa_input_command_tuning qa_input_command_defaults(qa_ruleset_id kind) {
    bool q1 = kind == QA_RULESET_NETQUAKE || kind == QA_RULESET_QUAKEWORLD;
    return (qa_input_command_tuning){.view = {.forward_speed = 200,
                                              .back_speed = 200,
                                              .side_speed = q1 ? 350 : 200,
                                              .up_speed = 200,
                                              .yaw_speed = 140,
                                              .pitch_speed = 150,
                                              .angle_multiplier = 1.5f,
                                              .move_multiplier = 2,
                                              .always_run = !q1},
                                     .mouse = qa_mouse_defaults(),
                                     .drift_speed = 500,
                                     .drift_delay = 0.15f};
}
void qa_input_command_clear(qa_input_command_builder *builder) {
    *builder = (qa_input_command_builder){.kind = builder->kind};
}
bool qa_input_command_angles(qa_input_command_builder *builder, qa_vec3 angles, qa_error *error) {
    if (!builder || !qa_vec_finite(angles)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input view angles");
        return false;
    }
    builder->angles = angles;
    return true;
}
void qa_input_command_center(qa_input_command_builder *builder, float delta_pitch) {
    builder->angles.x = -delta_pitch;
}
bool qa_input_command_impulse(qa_input_command_builder *builder, int32_t impulse, qa_error *error) {
    if (!builder || (builder->kind != QA_RULESET_NETQUAKE &&
        builder->kind != QA_RULESET_QUAKEWORLD) || impulse < 1 || impulse > UINT8_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Impulse requires an NQ/QW builder and value 1..255");
        return false;
    }
    builder->pending_impulse = (uint8_t)impulse;
    return true;
}

static bool valid(const qa_input_command_builder *builder, const qa_input_command_tuning *t,
                  const qa_seat_input_sample *s, const qa_input_command_frame *f,
                  double source_ms) {
    if (!builder || !t || !s || !f || f->kind != builder->kind || f->kind < QA_RULESET_NETQUAKE ||
        f->kind > QA_RULESET_Q3 || !qa_vec_finite(builder->angles) ||
        (builder->pending_impulse && builder->kind != QA_RULESET_NETQUAKE &&
         builder->kind != QA_RULESET_QUAKEWORLD) ||
        !qa_mouse_tuning_valid(&t->mouse) || !isfinite(source_ms) || source_ms < 0 ||
        !isfinite(s->frame_ms) || s->frame_ms <= 0 || !isfinite(s->gamepad.move.x) ||
        !isfinite(s->gamepad.move.y) || !isfinite(s->gamepad.look_degrees.x) ||
        !isfinite(s->gamepad.look_degrees.y))
        return false;
    const qa_view_input_tuning *v = &t->view;
    if (!isfinite(v->forward_speed) || !isfinite(v->back_speed) || !isfinite(v->side_speed) ||
        !isfinite(v->up_speed) || !isfinite(v->yaw_speed) || !isfinite(v->pitch_speed) ||
        !isfinite(v->angle_multiplier) || !isfinite(v->move_multiplier) ||
        !isfinite(t->drift_speed) || !isfinite(t->drift_delay))
        return false;
    if (f->has_pitch_drift && !isfinite(f->ideal_pitch))
        return false;
    if ((f->kind == QA_RULESET_Q2_CLASSIC || f->kind == QA_RULESET_Q2_RERELEASE) &&
        !qa_vec_finite(f->delta_angles))
        return false;
    if (f->kind == QA_RULESET_NETQUAKE && !isfinite(f->acknowledged_server_seconds))
        return false;
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
        if (!isfinite(s->buttons[i].fraction) || s->buttons[i].fraction < 0 ||
            s->buttons[i].fraction > 1)
            return false;
    return true;
}

bool qa_input_command_sample(qa_input_command_builder *builder, const qa_input_command_tuning *t,
                            const qa_seat_input_sample *s, const qa_input_command_frame *f,
                            double source_ms, qa_input_command_intent *out, qa_error *error) {
    if (!out || !valid(builder, t, s, f, source_ms)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input command frame or settings");
        return false;
    }
    qa_input_command_builder next = *builder;
    const qa_view_input_tuning *v = &t->view;
    bool q3 = f->kind == QA_RULESET_Q3;
    bool q1 = f->kind == QA_RULESET_NETQUAKE || f->kind == QA_RULESET_QUAKEWORLD;
    bool speed = active(s, QA_INPUT_WALK), strafe = active(s, QA_INPUT_STRAFE),
         klook = active(s, QA_INPUT_KLOOK);
    float seconds = (float)(source_ms / 1000),
          angle_speed = seconds * (speed ? v->angle_multiplier : 1);
    float pitch = next.angles.x, previous_pitch = pitch, yaw = next.angles.y, roll = next.angles.z;
    if (!strafe) {
        yaw -= angle_speed * v->yaw_speed * fraction(s, QA_INPUT_TURN_RIGHT);
        yaw += angle_speed * v->yaw_speed * fraction(s, QA_INPUT_TURN_LEFT);
        if (!isfinite(yaw)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input angle overflow");
            return false;
        }
        if (q1)
            yaw = qa_angle_mod(yaw);
    }
    if (klook && !q3) {
        pitch -= angle_speed * v->pitch_speed * fraction(s, QA_INPUT_FORWARD);
        pitch += angle_speed * v->pitch_speed * fraction(s, QA_INPUT_BACK);
    }
    pitch -= angle_speed * v->pitch_speed * fraction(s, QA_INPUT_LOOK_UP);
    pitch += angle_speed * v->pitch_speed * fraction(s, QA_INPUT_LOOK_DOWN);
    if (q1) {
        pitch = clamp(pitch, -70, 80);
        roll = clamp(roll, -50, 50);
    }
    bool running = speed != v->always_run;
    float move_speed = q3 ? (running ? stock[f->kind].move : stock[f->kind].walk) : 1;
    float forward = 0, side = 0, up = 0;
    if (strafe) {
        side = add(side, (q3 ? move_speed : v->side_speed) * fraction(s, QA_INPUT_TURN_RIGHT), q3);
        side = add(side, -(q3 ? move_speed : v->side_speed) * fraction(s, QA_INPUT_TURN_LEFT), q3);
    }
    side = add(side, (q3 ? move_speed : v->side_speed) * fraction(s, QA_INPUT_MOVE_RIGHT), q3);
    side = add(side, -(q3 ? move_speed : v->side_speed) * fraction(s, QA_INPUT_MOVE_LEFT), q3);
    float jump = q1 ? fraction(s, QA_INPUT_MOVE_UP)
                    : fmaxf(fraction(s, QA_INPUT_JUMP), fraction(s, QA_INPUT_MOVE_UP));
    float crouch = q1 ? fraction(s, QA_INPUT_MOVE_DOWN)
                      : fmaxf(fraction(s, QA_INPUT_CROUCH), fraction(s, QA_INPUT_MOVE_DOWN));
    up = add(up, (q3 ? move_speed : v->up_speed) * jump, q3);
    up = add(up, -(q3 ? move_speed : v->up_speed) * crouch, q3);
    if (!klook || q3) {
        forward =
            add(forward, (q3 ? move_speed : v->forward_speed) * fraction(s, QA_INPUT_FORWARD), q3);
        forward = add(forward, -(q3 ? move_speed : v->back_speed) * fraction(s, QA_INPUT_BACK), q3);
    }
    if (!q3 && running) {
        forward *= v->move_multiplier;
        side *= v->move_multiplier;
        up *= v->move_multiplier;
    }
    qa_mouse_move mouse;
    if (!qa_mouse_sample(&next.mouse, &t->mouse, s->mouse, s->frame_ms, strafe,
                         active(s, QA_INPUT_MLOOK), q3 ? f->sensitivity : 1, &mouse, error))
        return false;
    forward = add(forward, mouse.forward, q3);
    side = add(side, mouse.side, q3);
    yaw += mouse.yaw - s->gamepad.look_degrees.x;
    pitch += mouse.pitch + s->gamepad.look_degrees.y;
    float pad_forward = q3 ? move_speed : v->forward_speed * (running ? v->move_multiplier : 1);
    float pad_side = q3 ? move_speed : v->side_speed * (running ? v->move_multiplier : 1);
    forward = add(forward, s->gamepad.move.y * pad_forward, q3);
    side = add(side, s->gamepad.move.x * pad_side, q3);
    bool mlook = active(s, QA_INPUT_MLOOK);
    if (q1 && f->has_pitch_drift) {
        qa_pitch_drift_input drift = {
            .grounded = f->grounded,
            .disabled = f->drift_disabled,
            .manual = mlook || t->mouse.free_look ||
                      (klook && (active(s, QA_INPUT_FORWARD) || active(s, QA_INPUT_BACK))) ||
                      fraction(s, QA_INPUT_LOOK_UP) != 0 || fraction(s, QA_INPUT_LOOK_DOWN) != 0 ||
                      s->gamepad.look_degrees.y != 0,
            .start = !mlook && (next.previous_mouse_look || pressed(s, QA_INPUT_MLOOK)) &&
                     t->mouse.look_spring,
            .ideal_pitch = f->ideal_pitch,
            .forward = forward,
            .threshold = f->kind == QA_RULESET_QUAKEWORLD ? 200 : v->forward_speed,
            .speed = t->drift_speed,
            .delay = t->drift_delay};
        pitch = qa_pitch_drift_sample(&next.drift, pitch, seconds, &drift);
    }
    next.previous_mouse_look = mlook;
    uint64_t actions = 0;
    for (unsigned i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
        if (pressed(s, (qa_input_action)i)) actions |= UINT64_C(1) << i;
    if (q3) pitch = clamp(pitch, previous_pitch - 90, previous_pitch + 90);
    if (q1)
        pitch = clamp(pitch, -70, 80);
    if (f->kind == QA_RULESET_Q2_CLASSIC || f->kind == QA_RULESET_Q2_RERELEASE) {
        float delta = f->delta_angles.x;
        if (delta > 180)
            delta -= 360;
        if (pitch + delta < -360)
            pitch += 360;
        if (pitch + delta > 360)
            pitch -= 360;
        pitch = clamp(pitch, -89 - delta, 89 - delta);
    }
    next.angles = qa_v3(pitch, yaw, roll);
    if (!qa_vec_finite(next.angles) || !isfinite(forward) || !isfinite(side) || !isfinite(up)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input command overflow");
        return false;
    }
    qa_input_command_intent intent = {.angles = next.angles,
        .move = {(double)forward / PHYSICAL_MOVE_UNIT, (double)side / PHYSICAL_MOVE_UNIT,
                 (double)up / PHYSICAL_MOVE_UNIT},
        .actions = actions, .walking = !running, .game_focus = s->game_focus,
        .any_key_down = s->any_key_down,
        .impulse = q1 && next.pending_impulse ? next.pending_impulse : s->impulse};
    if (q1) next.pending_impulse = 0;
    *builder = next;
    *out = intent;
    return true;
}

static bool action(const qa_input_command_intent *intent, qa_input_action value) {
    return (intent->actions & (UINT64_C(1) << value)) != 0;
}
static int32_t signed_byte(int32_t value) {
    uint32_t byte = (uint32_t)value & 255;
    return byte >= 128 ? (int32_t)byte - 256 : (int32_t)byte;
}
static int32_t signed_word(uint32_t value) {
    uint32_t word = value & 65535;
    return word >= 32768 ? (int32_t)word - 65536 : (int32_t)word;
}
void qa_input_usercmd_build(const qa_input_command_intent *intent,
    const qa_input_command_frame *frame, double elapsed, qa_input_command_encoding encoding,
    qa_input_usercmd *out) {
    qa_ruleset_id kind = frame->kind;
    bool q1 = kind == QA_RULESET_NETQUAKE || kind == QA_RULESET_QUAKEWORLD;
    bool q3 = kind == QA_RULESET_Q3;
    const float scale = stock[kind].move;
    qa_vec3 move = qa_v3((float)(intent->move.x * PHYSICAL_MOVE_UNIT),
                        (float)(intent->move.y * PHYSICAL_MOVE_UNIT),
                        (float)(intent->move.z * PHYSICAL_MOVE_UNIT));
    if (intent->directional) {
        float speed = intent->speed * scale / stock[kind].source_speed;
        move = qa_vec_scale(intent->direction, speed);
        bool byte = q3 && encoding == QA_INPUT_COMMAND_SOURCE_Q3;
        if (byte) move = qa_v3((float)signed_byte(qa_source_float_to_i32(move.x)),
                              (float)signed_byte(qa_source_float_to_i32(move.y)),
                              (float)signed_byte(qa_source_float_to_i32(move.z)));
        if (action(intent, QA_INPUT_FORWARD)) move.x += scale;
        if (action(intent, QA_INPUT_BACK)) move.x -= scale;
        if (action(intent, QA_INPUT_MOVE_RIGHT)) move.y += scale;
        if (action(intent, QA_INPUT_MOVE_LEFT)) move.y -= scale;
        if (action(intent, QA_INPUT_JUMP) || action(intent, QA_INPUT_MOVE_UP)) move.z += scale;
        if (action(intent, QA_INPUT_CROUCH) || action(intent, QA_INPUT_MOVE_DOWN)) move.z -= scale;
        if (byte) move = qa_v3((float)signed_byte(qa_source_float_to_i32(move.x)),
                              (float)signed_byte(qa_source_float_to_i32(move.y)),
                              (float)signed_byte(qa_source_float_to_i32(move.z)));
    }
    uint32_t buttons = 0;
    if (q3) {
        for (unsigned i = 0; i < 15; ++i)
            if (action(intent, (qa_input_action)((unsigned)QA_INPUT_BUTTON0 + i))) buttons |= UINT32_C(1) << i;
        if (action(intent, QA_INPUT_ATTACK)) buttons |= 1;
        if (action(intent, QA_INPUT_USE)) buttons |= 4;
        if (intent->walking) buttons |= 16;
        if (!intent->game_focus) buttons |= 2;
        else if (intent->any_key_down) buttons |= 2048;
    } else {
        if (action(intent, QA_INPUT_ATTACK) && (q1 || frame->attack_allowed)) buttons |= 1;
        if ((q1 && action(intent, QA_INPUT_JUMP)) || (!q1 && action(intent, QA_INPUT_USE))) buttons |= 2;
        if (!q1 && intent->any_key_down && intent->game_focus) buttons |= 128;
        if (kind == QA_RULESET_Q2_RERELEASE) {
            if (action(intent, QA_INPUT_HOLSTER)) buttons |= 4;
            if (action(intent, QA_INPUT_JUMP) || action(intent, QA_INPUT_MOVE_UP)) buttons |= 8;
            if (action(intent, QA_INPUT_CROUCH) || action(intent, QA_INPUT_MOVE_DOWN)) buttons |= 16;
        }
    }
    qa_input_usercmd command = {.kind = kind, .sequence = frame->sequence,
        .angles = intent->angles, .milliseconds = trunc(elapsed > 250 ? 100 : elapsed),
        .buttons = buttons, .server_time_ms = q3 ? frame->server_time_ms : 0,
        .server_frame = kind == QA_RULESET_Q2_RERELEASE ? frame->server_frame : 0,
        .acknowledged_server_seconds = kind == QA_RULESET_NETQUAKE ? frame->acknowledged_server_seconds : 0,
        .weapon = q3 ? frame->weapon : 0, .light_level = kind == QA_RULESET_Q2_CLASSIC ? frame->light_level : 0,
        .impulse = kind == QA_RULESET_Q2_RERELEASE || q3 ? 0 : intent->impulse};
    if (q3 || kind == QA_RULESET_Q2_CLASSIC ||
        (kind == QA_RULESET_Q2_RERELEASE && intent->directional)) {
        qa_movement_command source = {.kind = kind, .angles = intent->angles}, converted;
        qa_input_command_basis from = {.kind = kind}, to = {.kind = kind,
            .words = q3 || kind == QA_RULESET_Q2_CLASSIC, .relative = intent->directional,
            .wrap_words = true, .delta_angles = frame->delta_angles};
        memcpy(to.delta_words, frame->delta_angle_words, sizeof(to.delta_words));
        qa_input_command_convert(&source, NULL, &from, &to, (qa_input_axis_rule){0}, &converted);
        if (kind == QA_RULESET_Q2_RERELEASE) command.angles = converted.angles;
        else {
            float *angles[] = {&command.angles.x, &command.angles.y, &command.angles.z};
            for (unsigned i = 0; i < 3; ++i) {
                uint32_t word = (uint32_t)converted.angle_words[i];
                command.angle_words[i] = encoding == QA_INPUT_COMMAND_SOURCE_Q3 ||
                    (kind == QA_RULESET_Q2_CLASSIC && encoding == QA_INPUT_COMMAND_NATIVE)
                    ? signed_word(word) : (int32_t)word;
                if (intent->directional && kind == QA_RULESET_Q2_CLASSIC)
                    *angles[i] = (float)signed_word(word) * (360.0f / 65536.0f);
            }
        }
    }
    if (q3 && encoding != QA_INPUT_COMMAND_SOURCE_Q3) {
        move.x = clamp(move.x, -scale, scale); move.y = clamp(move.y, -scale, scale);
        move.z = clamp(move.z, -scale, scale);
    } else if (kind == QA_RULESET_Q2_CLASSIC || kind == QA_RULESET_Q2_RERELEASE) {
        move.x = clamp(move.x, -scale, scale); move.y = clamp(move.y, -scale, scale);
    }
    if (kind == QA_RULESET_Q2_RERELEASE) move.z = 0;
    else move = qa_v3(truncf(move.x), truncf(move.y), truncf(move.z));
    command.move = move;
    *out = command;
}
float qa_input_command_units(qa_ruleset_id kind) {
    return kind == QA_RULESET_Q3 ? 127.0f :
        kind == QA_RULESET_NETQUAKE || kind == QA_RULESET_QUAKEWORLD ? 320.0f : 200.0f;
}
void qa_input_command_convert(const qa_movement_command *source, const qa_input_move_intent *precise,
    const qa_input_command_basis *from, const qa_input_command_basis *to,
    qa_input_axis_rule rule, qa_movement_command *out) {
    qa_movement_command result = *source;
    result.kind = to->kind;
    double input_units = from->units != 0 ? from->units : qa_input_command_units(from->kind);
    double output_units = to->units != 0 ? to->units : qa_input_command_units(to->kind);
    const double moves[] = {precise ? precise->x : source->forward_move,
        precise ? precise->y : source->side_move, precise ? precise->z : source->up_move};
    float *outputs[] = {&result.forward_move, &result.side_move, &result.up_move};
    for (unsigned i = 0; i < 3; ++i) {
        double value;
        if (rule.float_product) {
            float ratio = (float)(output_units / input_units);
            value = (float)((float)moves[i] * ratio);
        } else value = rule.ratio_first ? (double)moves[i] * (output_units / input_units) :
            (double)moves[i] * output_units / input_units;
        if (rule.clamp) value = fmax(rule.minimum, fmin(rule.maximum, value));
        switch (rule.quantization) {
        case QA_INPUT_AXIS_EXACT: break;
        case QA_INPUT_AXIS_TRUNCATE: value = trunc(value); break;
        case QA_INPUT_AXIS_NEAREST: value = (double)lrintf((float)value); break;
        case QA_INPUT_AXIS_SHORT: value = (int32_t)(uint16_t)(uint32_t)qa_source_float_to_i32((float)value);
            if (value > INT16_MAX) value -= 65536;
            break;
        }
        *outputs[i] = (float)value;
    }
    const float degrees[] = {source->angles.x, source->angles.y, source->angles.z};
    const float from_delta[] = {from->delta_angles.x, from->delta_angles.y, from->delta_angles.z};
    const float to_delta[] = {to->delta_angles.x, to->delta_angles.y, to->delta_angles.z};
    float *angles[] = {&result.angles.x, &result.angles.y, &result.angles.z};
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t word = (uint32_t)source->angle_words[i];
        float absolute;
        double wide;
        if (from->words) {
            word += from->relative ? (uint32_t)from->delta_words[i] : 0;
            double value = from->wrap_words ? (double)(word & 65535u) :
                (double)source->angle_words[i] + (from->relative ? from->delta_words[i] : 0);
            wide = value * 360.0 / 65536.0; absolute = (float)wide;
        } else {
            absolute = from->relative ? degrees[i] + from_delta[i] : degrees[i];
            wide = absolute; word = qa_angle_to_word(absolute);
        }
        if (from->repack_words) word = qa_angle_to_word(absolute);
        if (to->words) {
            word -= to->relative ? (uint32_t)to->delta_words[i] : 0;
            if (to->wrap_words) word &= 65535u;
            memcpy(&result.angle_words[i], &word, sizeof(word));
            *angles[i] = absolute;
        } else {
            result.angle_words[i] = 0;
            *angles[i] = to->relative ? to->wide_delta ? (float)(wide - to_delta[i]) :
                absolute - to_delta[i] : absolute;
        }
    }
    *out = result;
}
void qa_input_usercmd_project(const qa_input_usercmd *source, qa_movement_command *out) {
    *out = (qa_movement_command){.kind = source->kind, .sequence = source->sequence,
        .milliseconds = (uint32_t)source->milliseconds, .server_time_ms = (int32_t)source->server_time_ms,
        .server_frame = (int32_t)source->server_frame, .acknowledged_server_seconds = source->acknowledged_server_seconds,
        .angles = source->angles, .forward_move = source->move.x, .side_move = source->move.y,
        .up_move = source->move.z, .buttons = source->buttons, .impulse = source->impulse,
        .weapon = (uint8_t)(int32_t)source->weapon, .light_level = (uint8_t)(int32_t)source->light_level};
    for (unsigned i = 0; i < 3; ++i) out->angle_words[i] = source->angle_words[i];
}
bool qa_input_command_build(qa_input_command_builder *builder, const qa_input_command_tuning *tuning,
    const qa_seat_input_sample *sample, const qa_input_command_frame *frame, double elapsed,
    qa_movement_command *out, qa_error *error) {
    if (!out || !builder) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input command frame or settings");
        return false;
    }
    qa_input_command_builder next = *builder;
    qa_input_command_intent intent;
    if (!qa_input_command_sample(&next, tuning, sample, frame, elapsed, &intent, error)) return false;
    qa_input_usercmd command;
    qa_input_usercmd_build(&intent, frame, elapsed, QA_INPUT_COMMAND_NATIVE, &command);
    qa_input_usercmd_project(&command, out);
    *builder = next;
    return true;
}
