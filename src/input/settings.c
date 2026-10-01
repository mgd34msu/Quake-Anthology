#include "qa/input.h"
#include <stdio.h>

struct setting {
    const char *name, *value;
    uint32_t flags;
};
static bool declare(qa_cvars *vars, const struct setting *settings, size_t count, qa_error *error) {
    for (size_t i = 0; i < count; ++i) {
        const qa_cvar_view *old = qa_cvars_find(vars, settings[i].name);
        if ((!old || old->console_created) &&
            !qa_cvars_register(vars, settings[i].name, settings[i].value, settings[i].flags, 0,
                               "Input setting", error))
            return false;
    }
    return true;
}
bool qa_input_device_settings_register(qa_cvars *vars, qa_error *error) {
    qa_console_dialect d = qa_cvars_dialect(vars);
    uint32_t latch = d == QA_CONSOLE_Q3 ? QA_CVAR_LATCH : d >= QA_CONSOLE_Q2 ? QA_Q2_CVAR_LATCH : 0;
    struct setting settings[] = {
        {"in_midi", "0", QA_CVAR_ARCHIVE},
        {"in_midiport", "1", QA_CVAR_ARCHIVE},
        {"in_midichannel", "1", QA_CVAR_ARCHIVE},
        {"in_mididevice", "0", QA_CVAR_ARCHIVE},
        {"in_midiseat", "1", QA_CVAR_ARCHIVE},
        {"in_mouse", "1", QA_CVAR_ARCHIVE},
        {"in_dgamouse", "1", QA_CVAR_ARCHIVE},
        {"in_subframe", "1", QA_CVAR_ARCHIVE},
        {"in_nograb", "0", 0},
        {"in_joystick", "0", QA_CVAR_ARCHIVE | latch},
        {"in_debugjoystick", "0", d == QA_CONSOLE_Q3 ? QA_CVAR_TEMPORARY : 0},
        {"joy_threshold", "0.15", QA_CVAR_ARCHIVE},
        {"in_joystickProfile", "linux", QA_CVAR_ARCHIVE | latch},
        {"in_joyBallScale", "0.02", QA_CVAR_ARCHIVE},
        {"in_joystickSeat", "1", QA_CVAR_ARCHIVE}};
#ifdef _WIN32
    settings[12].value = "windows";
#endif
    return declare(vars, settings, sizeof(settings) / sizeof(*settings), error);
}
static bool settings_owner(const qa_cvars *vars, qa_movement_kind kind, qa_error *error) {
    if (!vars || kind < QA_MOVEMENT_NETQUAKE || kind > QA_MOVEMENT_Q3) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing input settings owner or movement dialect");
        return false;
    }
    return true;
}
static bool mouse_register(qa_cvars *vars, qa_error *error) {
    const struct setting settings[] = {{"v_centerspeed", "500", 0},
                                       {"v_centermove", "0.15", 0},
                                       {"sensitivity", "3", QA_CVAR_ARCHIVE},
                                       {"cl_mouseAccel", "0", QA_CVAR_ARCHIVE},
                                       {"m_filter", "0", QA_CVAR_ARCHIVE},
                                       {"m_yaw", "0.022", QA_CVAR_ARCHIVE},
                                       {"m_pitch", "0.022", QA_CVAR_ARCHIVE},
                                       {"m_side", "0.8", QA_CVAR_ARCHIVE},
                                       {"m_forward", "1", QA_CVAR_ARCHIVE},
                                       {"lookspring", "0", QA_CVAR_ARCHIVE},
                                       {"lookstrafe", "0", QA_CVAR_ARCHIVE},
                                       {"freelook", "1", QA_CVAR_ARCHIVE}};
    return declare(vars, settings, sizeof(settings) / sizeof(*settings), error);
}
static bool movement_register(qa_cvars *vars, qa_movement_kind kind, qa_error *error) {
    bool q1 = kind == QA_MOVEMENT_NETQUAKE || kind == QA_MOVEMENT_QUAKEWORLD;
    const struct setting settings[] = {{"cl_forwardspeed", "200", QA_CVAR_ARCHIVE},
                                       {"cl_backspeed", "200", QA_CVAR_ARCHIVE},
                                       {"cl_sidespeed", q1 ? "350" : "200", QA_CVAR_ARCHIVE},
                                       {"cl_upspeed", "200", QA_CVAR_ARCHIVE},
                                       {"cl_yawspeed", "140", QA_CVAR_ARCHIVE},
                                       {"cl_pitchspeed", "150", QA_CVAR_ARCHIVE},
                                       {"cl_anglespeedkey", "1.5", QA_CVAR_ARCHIVE},
                                       {"cl_movespeedkey", "2", QA_CVAR_ARCHIVE}};
    return declare(vars, settings, sizeof(settings) / sizeof(*settings), error);
}
static bool run_register(qa_cvars *vars, qa_movement_kind kind, qa_error *error) {
    bool q1 = kind == QA_MOVEMENT_NETQUAKE || kind == QA_MOVEMENT_QUAKEWORLD;
    const struct setting setting = {"cl_run", q1 ? "0" : "1", QA_CVAR_ARCHIVE};
    return declare(vars, &setting, 1, error);
}
bool qa_input_mouse_settings_register(qa_cvars *vars, qa_movement_kind selected, qa_error *error) {
    if (!settings_owner(vars, selected, error) || !mouse_register(vars, error)) return false;
    const qa_cvar_view *run = qa_cvars_find(vars, "cl_run");
    qa_console_dialect dialect = qa_cvars_dialect(vars);
    if (run && !run->console_created && (dialect == QA_CONSOLE_Q1 || dialect == QA_CONSOLE_QW))
        return qa_cvars_add_flags(vars, "cl_run", QA_CVAR_ARCHIVE, error);
    bool q1 = selected == QA_MOVEMENT_NETQUAKE || selected == QA_MOVEMENT_QUAKEWORLD;
    return qa_cvars_register(vars, "cl_run", q1 ? "0" : "1", QA_CVAR_ARCHIVE, 0, "Input setting", error);
}
bool qa_input_movement_settings_register(qa_cvars *vars, qa_movement_kind kind, qa_error *error) {
    return settings_owner(vars, kind, error) && movement_register(vars, kind, error);
}
bool qa_input_settings_register(qa_cvars *vars, qa_movement_kind kind, qa_error *error) {
    /* Preserve the composed registry's original declaration and observer order. */
    return settings_owner(vars, kind, error) && mouse_register(vars, error) &&
        movement_register(vars, kind, error) && run_register(vars, kind, error);
}
static float value(const qa_cvars *vars, const char *name, float fallback) {
    const qa_cvar_view *v = qa_cvars_find(vars, name);
    return v ? v->number : fallback;
}
bool qa_input_settings_read_routed(const qa_cvars *mouse, const qa_cvars *movement,
                                   qa_movement_kind kind, qa_input_command_tuning *out,
                                   qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing input settings owner");
        return false;
    }
    if (!settings_owner(mouse, kind, error) || !settings_owner(movement, kind, error)) return false;
    qa_input_command_tuning t = qa_input_command_defaults(kind);
    qa_mouse_tuning *m = &t.mouse;
    qa_view_input_tuning *v = &t.view;
    m->sensitivity = value(mouse, "sensitivity", m->sensitivity);
    m->acceleration = value(mouse, "cl_mouseAccel", m->acceleration);
    const qa_cvar_view *filter = qa_cvars_find(mouse, "m_filter");
    m->filter = filter && (qa_cvars_dialect(mouse) == QA_CONSOLE_Q3 ? filter->integer != 0
                                                                   : filter->number != 0);
    m->yaw = value(mouse, "m_yaw", m->yaw);
    float pitch = value(mouse, "m_pitch", m->pitch);
    m->pitch = fabsf(pitch);
    m->invert_pitch = signbit(pitch) != 0;
    m->side = value(mouse, "m_side", m->side);
    m->forward = value(mouse, "m_forward", m->forward);
    m->free_look = value(mouse, "freelook", 1) != 0;
    m->look_spring = value(mouse, "lookspring", 0) != 0;
    m->look_strafe = value(mouse, "lookstrafe", 0) != 0;
    t.drift_speed = value(mouse, "v_centerspeed", t.drift_speed);
    t.drift_delay = value(mouse, "v_centermove", t.drift_delay);
    v->forward_speed = value(movement, "cl_forwardspeed", v->forward_speed);
    v->back_speed = value(movement, "cl_backspeed", v->back_speed);
    v->side_speed = value(movement, "cl_sidespeed", v->side_speed);
    v->up_speed = value(movement, "cl_upspeed", v->up_speed);
    v->yaw_speed = value(movement, "cl_yawspeed", v->yaw_speed);
    v->pitch_speed = value(movement, "cl_pitchspeed", v->pitch_speed);
    v->angle_multiplier = value(movement, "cl_anglespeedkey", v->angle_multiplier);
    v->move_multiplier = value(movement, "cl_movespeedkey", v->move_multiplier);
    v->always_run = value(mouse, "cl_run", v->always_run ? 1 : 0) != 0;
    const float numeric[] = {
        t.drift_speed, t.drift_delay, v->forward_speed, v->back_speed,       v->side_speed,
        v->up_speed,   v->yaw_speed,  v->pitch_speed,   v->angle_multiplier, v->move_multiplier};
    for (size_t i = 0; i < sizeof(numeric) / sizeof(*numeric); ++i)
        if (!isfinite(numeric[i])) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Nonfinite input setting");
            return false;
        }
    if (!qa_mouse_tuning_valid(m)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid mouse settings");
        return false;
    }
    *out = t;
    return true;
}
bool qa_input_settings_read(const qa_cvars *vars, qa_movement_kind kind,
                            qa_input_command_tuning *out, qa_error *error) {
    return qa_input_settings_read_routed(vars, vars, kind, out, error);
}
bool qa_input_mouse_settings_write(qa_cvars *vars, const qa_mouse_tuning *t, qa_error *error) {
    if (!vars || !qa_mouse_tuning_valid(t)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid mouse settings");
        return false;
    }
    const char *const names[] = {"sensitivity", "cl_mouseAccel", "m_filter",  "m_yaw",
                                 "m_pitch",     "m_side",        "m_forward", "lookspring",
                                 "lookstrafe",  "freelook"};
    float values[] = {t->sensitivity,
                      t->acceleration,
                      t->filter ? 1 : 0,
                      t->yaw,
                      t->pitch * (t->invert_pitch ? -1 : 1),
                      t->side,
                      t->forward,
                      t->look_spring ? 1 : 0,
                      t->look_strafe ? 1 : 0,
                      t->free_look ? 1 : 0};
    for (size_t i = 0; i < sizeof(values) / sizeof(*values); ++i) {
        char text[48];
        (void)snprintf(text, sizeof(text), "%.9g", (double)values[i]);
        if (!qa_cvars_set(vars, names[i], text, false, error))
            return false;
    }
    return true;
}
