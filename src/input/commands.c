#include "qa/input.h"

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
qa_input_command_tuning qa_input_command_defaults(qa_movement_kind kind) {
    bool q1 = kind == QA_MOVEMENT_NETQUAKE || kind == QA_MOVEMENT_QUAKEWORLD;
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
    if (!builder || (builder->kind != QA_MOVEMENT_NETQUAKE &&
        builder->kind != QA_MOVEMENT_QUAKEWORLD) || impulse < 1 || impulse > UINT8_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Impulse requires an NQ/QW builder and value 1..255");
        return false;
    }
    builder->pending_impulse = (uint8_t)impulse;
    return true;
}

static bool valid(const qa_input_command_builder *builder, const qa_input_command_tuning *t,
                  const qa_seat_input_sample *s, const qa_input_command_frame *f,
                  double source_ms) {
    if (!builder || !t || !s || !f || f->kind != builder->kind || f->kind < QA_MOVEMENT_NETQUAKE ||
        f->kind > QA_MOVEMENT_Q3 || !qa_vec_finite(builder->angles) ||
        (builder->pending_impulse && builder->kind != QA_MOVEMENT_NETQUAKE &&
         builder->kind != QA_MOVEMENT_QUAKEWORLD) ||
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
    if ((f->kind == QA_MOVEMENT_Q2_CLASSIC || f->kind == QA_MOVEMENT_Q2_RERELEASE) &&
        !qa_vec_finite(f->delta_angles))
        return false;
    if (f->kind == QA_MOVEMENT_NETQUAKE && !isfinite(f->acknowledged_server_seconds))
        return false;
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
        if (!isfinite(s->buttons[i].fraction) || s->buttons[i].fraction < 0 ||
            s->buttons[i].fraction > 1)
            return false;
    return true;
}

bool qa_input_command_build(qa_input_command_builder *builder, const qa_input_command_tuning *t,
                            const qa_seat_input_sample *s, const qa_input_command_frame *f,
                            double source_ms, qa_movement_command *out, qa_error *error) {
    if (!out || !valid(builder, t, s, f, source_ms)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input command frame or settings");
        return false;
    }
    qa_input_command_builder next = *builder;
    const qa_view_input_tuning *v = &t->view;
    bool q3 = f->kind == QA_MOVEMENT_Q3;
    bool q1 = f->kind == QA_MOVEMENT_NETQUAKE || f->kind == QA_MOVEMENT_QUAKEWORLD;
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
    float move_speed = q3 ? (running ? 127 : 64) : 1;
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
            .threshold = f->kind == QA_MOVEMENT_QUAKEWORLD ? 200 : v->forward_speed,
            .speed = t->drift_speed,
            .delay = t->drift_delay};
        pitch = qa_pitch_drift_sample(&next.drift, pitch, seconds, &drift);
    }
    next.previous_mouse_look = mlook;
    uint32_t buttons = 0;
    if (q3) {
        for (unsigned i = 0; i < 15; ++i)
            if (pressed(s, (qa_input_action)((unsigned)QA_INPUT_BUTTON0 + i)))
                buttons |= UINT32_C(1) << i;
        if (pressed(s, QA_INPUT_ATTACK))
            buttons |= 1;
        if (pressed(s, QA_INPUT_USE))
            buttons |= 4;
        if (!running)
            buttons |= 16;
        if (!s->game_focus)
            buttons |= 2;
        else if (s->any_key_down)
            buttons |= 2048;
        pitch = clamp(pitch, previous_pitch - 90, previous_pitch + 90);
    } else {
        if (pressed(s, QA_INPUT_ATTACK) && (q1 || f->attack_allowed))
            buttons |= 1;
        if ((q1 && pressed(s, QA_INPUT_JUMP)) || (!q1 && pressed(s, QA_INPUT_USE)))
            buttons |= 2;
        if (!q1 && s->any_key_down && s->game_focus)
            buttons |= 128;
        if (f->kind == QA_MOVEMENT_Q2_RERELEASE) {
            if (pressed(s, QA_INPUT_HOLSTER))
                buttons |= 4;
            if (pressed(s, QA_INPUT_JUMP) || pressed(s, QA_INPUT_MOVE_UP))
                buttons |= 8;
            if (pressed(s, QA_INPUT_CROUCH) || pressed(s, QA_INPUT_MOVE_DOWN))
                buttons |= 16;
        }
    }
    if (q1)
        pitch = clamp(pitch, -70, 80);
    if (f->kind == QA_MOVEMENT_Q2_CLASSIC || f->kind == QA_MOVEMENT_Q2_RERELEASE) {
        float delta = f->delta_angles.x;
        if (delta > 180)
            delta -= 360;
        if (pitch + delta < -360)
            pitch += 360;
        if (pitch + delta > 360)
            pitch -= 360;
        pitch = clamp(pitch, -89 - delta, 89 - delta);
        forward = clamp(forward, -400, 400);
        side = clamp(side, -400, 400);
    }
    next.angles = qa_v3(pitch, yaw, roll);
    if (!qa_vec_finite(next.angles) || !isfinite(forward) || !isfinite(side) || !isfinite(up)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Input command overflow");
        return false;
    }
    qa_movement_command command = {.kind = f->kind,
                                   .sequence = f->sequence,
                                   .angles = next.angles,
                                   .milliseconds = (uint32_t)(source_ms > 250 ? 100 : source_ms),
                                   .buttons = buttons};
    if (q3) {
        command.server_time_ms = f->server_time_ms;
        command.weapon = f->weapon;
        forward = clamp(forward, -127, 127);
        side = clamp(side, -127, 127);
        up = clamp(up, -127, 127);
    } else if (f->kind == QA_MOVEMENT_Q2_RERELEASE) {
        command.server_frame = f->server_frame;
        up = 0;
    } else {
        command.impulse = q1 && next.pending_impulse ? next.pending_impulse : s->impulse;
        if (q1) next.pending_impulse = 0;
        if (f->kind == QA_MOVEMENT_Q2_CLASSIC)
            command.light_level = f->light_level;
        if (f->kind == QA_MOVEMENT_NETQUAKE)
            command.acknowledged_server_seconds = f->acknowledged_server_seconds;
    }
    if (q3 || f->kind == QA_MOVEMENT_Q2_CLASSIC) {
        command.angle_words[0] = qa_angle_to_word(pitch);
        command.angle_words[1] = qa_angle_to_word(yaw);
        command.angle_words[2] = qa_angle_to_word(roll);
    }
    bool integral = f->kind != QA_MOVEMENT_Q2_RERELEASE;
    command.forward_move = integral ? truncf(forward) : forward;
    command.side_move = integral ? truncf(side) : side;
    command.up_move = integral ? truncf(up) : up;
    *builder = next;
    *out = command;
    return true;
}
