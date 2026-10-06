#include "internal.h"
#include <float.h>
#include <limits.h>
#include <stddef.h>

bool settings_fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_FORMAT, 0, "%s", message);
    return false;
}
bool settings_object(const qa_json_document *d, qa_json_id id, qa_error *e) {
    return qa_json_type(d, id) == QA_JSON_OBJECT || settings_fail(e, "Expected settings object");
}
bool settings_version(const qa_json_document *d, qa_json_id root, qa_error *e) {
    uint64_t version;
    return settings_object(d, root, e) &&
           qa_json_u64(d, qa_json_get(d, root, "version"), &version, e) &&
           (version == 1 || settings_fail(e, "Unsupported settings version"));
}
bool settings_string(const qa_json_document *d, qa_json_id id, char **out, qa_error *e) {
    qa_buffer text;
    if (!qa_json_string(d, id, &text, e))
        return false;
    if (memchr(text.data, 0, text.size)) {
        qa_buffer_free(&text);
        return settings_fail(e, "NUL in settings string");
    }
    *out = (char *)text.data;
    return true;
}
bool settings_choice(const qa_json_document *d, qa_json_id id, const char *const *names,
                     size_t count, unsigned *out, qa_error *e) {
    for (size_t i = 0; i < count; ++i)
        if (qa_json_string_equal(d, id, names[i])) {
            *out = (unsigned)i;
            return true;
        }
    return settings_fail(e, "Unknown settings choice");
}
bool settings_u32(const qa_json_document *d, qa_json_id id, uint32_t *out, qa_error *e) {
    uint64_t n;
    if (!qa_json_u64(d, id, &n, e))
        return false;
    if (n > UINT32_MAX)
        return settings_fail(e, "Settings integer exceeds native range");
    *out = (uint32_t)n;
    return true;
}
bool settings_float(const qa_json_document *d, qa_json_id id, float *out, qa_error *e) {
    double n;
    if (!qa_json_number(d, id, &n, e))
        return false;
    if (!isfinite(n) || fabs(n) > FLT_MAX)
        return settings_fail(e, "Settings number exceeds finite float range");
    *out = (float)n;
    return true;
}
bool settings_guid(const char *guid, bool lowercase) {
    return guid && strlen(guid) == 32 &&
           strspn(guid, lowercase ? "0123456789abcdef" : "0123456789abcdefABCDEF") == 32;
}
bool settings_finish(qa_json_writer *writer, qa_buffer *out, qa_error *e) {
    bool ok = qa_json_writer_finish(writer, out, e);
    qa_json_writer_destroy(writer);
    return ok;
}
void settings_key_string(qa_json_writer *w, const char *key, const char *value) {
    qa_json_writer_key(w, key);
    qa_json_writer_string(w, value);
}
void settings_key_number(qa_json_writer *w, const char *key, double value) {
    qa_json_writer_key(w, key);
    qa_json_writer_number(w, value);
}
void settings_key_bool(qa_json_writer *w, const char *key, bool value) {
    qa_json_writer_key(w, key);
    qa_json_writer_bool(w, value);
}

typedef struct field {
    const char *name;
    size_t offset;
    bool boolean, optional;
} field;
#define F(type, member, name) {name, offsetof(type, member), false, false}
#define B(type, member, name) {name, offsetof(type, member), true, false}
#define BO(type, member, name) {name, offsetof(type, member), true, true}
static const field mouse_fields[] = {F(qa_mouse_tuning, sensitivity, "sensitivity"),
                                     F(qa_mouse_tuning, acceleration, "acceleration"),
                                     F(qa_mouse_tuning, yaw, "yaw"),
                                     F(qa_mouse_tuning, pitch, "pitch"),
                                     F(qa_mouse_tuning, side, "side"),
                                     F(qa_mouse_tuning, forward, "forward"),
                                     B(qa_mouse_tuning, filter, "filter"),
                                     B(qa_mouse_tuning, free_look, "freeLook"),
                                     B(qa_mouse_tuning, invert_pitch, "invertPitch"),
                                     BO(qa_mouse_tuning, look_spring, "lookSpring"),
                                     BO(qa_mouse_tuning, look_strafe, "lookStrafe")};
static const field pad_fields[] = {F(qa_gamepad_tuning, yaw_speed, "yawDegreesPerSecond"),
                                   F(qa_gamepad_tuning, pitch_speed, "pitchDegreesPerSecond"),
                                   F(qa_gamepad_tuning, forward_sensitivity, "forwardSensitivity"),
                                   F(qa_gamepad_tuning, side_sensitivity, "sideSensitivity"),
                                   F(qa_gamepad_tuning, trigger_threshold, "triggerThreshold"),
                                   B(qa_gamepad_tuning, swap_sticks, "swapSticks"),
                                   B(qa_gamepad_tuning, invert_pitch, "invertPitch")};
static const field gyro_fields[] = {
    F(qa_gamepad_tuning, gyro_yaw_sensitivity, "yawSensitivity"),
    F(qa_gamepad_tuning, gyro_pitch_sensitivity, "pitchSensitivity"),
    B(qa_gamepad_tuning, gyro_enabled, "enabled")};
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))
static bool fields_read(const qa_json_document *d, qa_json_id id, void *out, const field *fields,
                        size_t count, qa_error *e) {
    if (!settings_object(d, id, e))
        return false;
    for (size_t i = 0; i < count; ++i) {
        qa_json_id value = qa_json_get(d, id, fields[i].name);
        if (value == QA_JSON_NONE && fields[i].optional)
            continue;
        void *target = (unsigned char *)out + fields[i].offset;
        if (fields[i].boolean ? !qa_json_bool(d, value, target, e)
                              : !settings_float(d, value, target, e))
            return false;
    }
    return true;
}
static void fields_write(qa_json_writer *w, const void *value, const field *fields, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const void *source = (const unsigned char *)value + fields[i].offset;
        if (fields[i].boolean)
            settings_key_bool(w, fields[i].name, *(const bool *)source);
        else
            settings_key_number(w, fields[i].name, *(const float *)source);
    }
}
static const char *const curves[] = {"radial", "axial"}, *const yaw_axes[] = {"y", "z"};
static const char *const physical_names[] = {"key", "mouse-button", "controller-button",
                                             "controller-axis"};
static const char *const axis_names[] = {"left-x",  "left-y",       "right-x",
                                         "right-y", "left-trigger", "right-trigger"};
static const char *const selection_names[] = {"automatic", "none", "device", "serial"};
static const char *const actions[QA_INPUT_ACTION_COUNT] = {
    "forward",         "back",       "move-left", "move-right", "move-up", "move-down",
    "turn-left",       "turn-right", "look-up",   "look-down",  "jump",    "crouch",
    "attack",          "use",        "holster",   "walk",       "strafe",  "klook",
    "mlook",           "button0",    "button1",   "button2",    "button3", "button4",
    "button5",         "button6",    "button7",   "button8",    "button9", "button10",
    "button11",        "button12",   "button13",  "button14",   "scores",  "next-weapon",
    "previous-weapon", "menu"};
static bool curve_read(const qa_json_document *d, qa_json_id id, qa_stick_curve *out, qa_error *e) {
    unsigned kind;
    if (!settings_object(d, id, e) ||
        !settings_choice(d, qa_json_get(d, id, "kind"), curves, COUNT(curves), &kind, e) ||
        !settings_float(d, qa_json_get(d, id, "deadzone"), &out->deadzone, e) ||
        !settings_float(d, qa_json_get(d, id, "exponent"), &out->exponent, e))
        return false;
    out->kind = (qa_stick_kind)kind;
    return kind == QA_STICK_AXIAL ||
           settings_float(d, qa_json_get(d, id, "outerThreshold"), &out->outer_threshold, e);
}
static void curve_write(qa_json_writer *w, const qa_stick_curve *curve) {
    qa_json_writer_object(w);
    settings_key_string(w, "kind", curves[curve->kind]);
    settings_key_number(w, "deadzone", curve->deadzone);
    settings_key_number(w, "exponent", curve->exponent);
    if (curve->kind == QA_STICK_RADIAL)
        settings_key_number(w, "outerThreshold", curve->outer_threshold);
    qa_json_writer_end(w);
}
static bool gamepad_read(const qa_json_document *d, qa_json_id id, qa_gamepad_tuning *out,
                         qa_error *e) {
    unsigned yaw;
    qa_json_id gyro = qa_json_get(d, id, "gyro");
    if (!fields_read(d, id, out, pad_fields, COUNT(pad_fields), e) ||
        !fields_read(d, gyro, out, gyro_fields, COUNT(gyro_fields), e) ||
        !settings_choice(d, qa_json_get(d, gyro, "yawAxis"), yaw_axes, COUNT(yaw_axes), &yaw, e) ||
        !curve_read(d, qa_json_get(d, id, "move"), &out->move, e) ||
        !curve_read(d, qa_json_get(d, id, "look"), &out->look, e))
        return false;
    out->gyro_yaw_axis = (qa_gyro_yaw_axis)yaw;
    return qa_gamepad_tuning_valid(out) || settings_fail(e, "Invalid gamepad tuning");
}
static void gamepad_write(qa_json_writer *w, const qa_gamepad_tuning *pad) {
    qa_json_writer_object(w);
    fields_write(w, pad, pad_fields, COUNT(pad_fields));
    qa_json_writer_key(w, "move");
    curve_write(w, &pad->move);
    qa_json_writer_key(w, "look");
    curve_write(w, &pad->look);
    qa_json_writer_key(w, "gyro");
    qa_json_writer_object(w);
    fields_write(w, pad, gyro_fields, COUNT(gyro_fields));
    settings_key_string(w, "yawAxis", yaw_axes[pad->gyro_yaw_axis]);
    qa_json_writer_end(w);
    qa_json_writer_end(w);
}
static bool physical_read(const qa_json_document *d, qa_json_id input, qa_physical_input *out,
                          qa_error *e) {
    unsigned kind;
    if (!settings_object(d, input, e) ||
        !settings_choice(d, qa_json_get(d, input, "kind"), physical_names, COUNT(physical_names),
                         &kind, e))
        return false;
    out->kind = (qa_physical_kind)kind;
    if (kind == QA_PHYSICAL_BUTTON || kind == QA_PHYSICAL_AXIS) {
        uint32_t device;
        if (!settings_u32(d, qa_json_get(d, input, "device"), &device, e) || device > INT32_MAX)
            return settings_fail(e, "Invalid controller device ID");
        out->device = (int32_t)device;
    }
    if (kind == QA_PHYSICAL_AXIS) {
        unsigned axis, direction;
        static const char *const directions[] = {"negative", "positive"};
        if (!settings_choice(d, qa_json_get(d, input, "axis"), axis_names, COUNT(axis_names), &axis,
                             e) ||
            !settings_choice(d, qa_json_get(d, input, "direction"), directions, 2, &direction, e))
            return false;
        out->code = axis;
        out->positive = direction != 0;
    } else if (!settings_u32(d, qa_json_get(d, input, kind == QA_PHYSICAL_KEY ? "code" : "button"),
                             &out->code, e))
        return false;
    if (!qa_input_physical_valid(*out))
        return settings_fail(e, "Invalid physical input binding");
    return true;
}
static bool binding_read(const qa_json_document *d, qa_json_id id, qa_input_binding *out,
                         qa_error *e) {
    qa_json_id input = qa_json_get(d, id, "input"), target = qa_json_get(d, id, "target");
    if (!settings_object(d, id, e) || !settings_object(d, target, e) ||
        !physical_read(d, input, &out->input, e)) return false;
    if (qa_json_string_equal(d, qa_json_get(d, target, "kind"), "action")) {
        unsigned action;
        if (!settings_choice(d, qa_json_get(d, target, "action"), actions, COUNT(actions), &action,
                             e))
            return false;
        out->kind = QA_BIND_ACTION;
        out->action = (qa_input_action)action;
    } else if (qa_json_string_equal(d, qa_json_get(d, target, "kind"), "command")) {
        char *command;
        if (!settings_string(d, qa_json_get(d, target, "text"), &command, e))
            return false;
        out->kind = QA_BIND_COMMAND;
        out->command = command;
    } else
        return settings_fail(e, "Unknown binding target");
    return true;
}
static bool binding_valid(const qa_input_binding *binding) {
    return qa_input_physical_valid(binding->input) &&
           ((binding->kind == QA_BIND_ACTION && binding->action >= 0 &&
             binding->action < QA_INPUT_ACTION_COUNT) ||
            (binding->kind == QA_BIND_COMMAND && binding->command));
}
static void physical_write(qa_json_writer *w, const qa_physical_input *input) {
    qa_json_writer_object(w);
    settings_key_string(w, "kind", physical_names[input->kind]);
    if (input->kind == QA_PHYSICAL_BUTTON || input->kind == QA_PHYSICAL_AXIS)
        settings_key_number(w, "device", input->device);
    if (input->kind == QA_PHYSICAL_AXIS) {
        settings_key_string(w, "axis", axis_names[input->code]);
        settings_key_string(w, "direction", input->positive ? "positive" : "negative");
    } else
        settings_key_number(w, input->kind == QA_PHYSICAL_KEY ? "code" : "button", input->code);
    qa_json_writer_end(w);
}
static void binding_write(qa_json_writer *w, const qa_input_binding *b) {
    qa_json_writer_object(w);
    qa_json_writer_key(w, "input");
    physical_write(w, &b->input);
    qa_json_writer_key(w, "target");
    qa_json_writer_object(w);
    settings_key_string(w, "kind", b->kind == QA_BIND_ACTION ? "action" : "command");
    settings_key_string(w, b->kind == QA_BIND_ACTION ? "action" : "text",
                        b->kind == QA_BIND_ACTION ? actions[b->action] : b->command);
    qa_json_writer_end(w);
    qa_json_writer_end(w);
}
static bool controller_valid(const qa_controller_selection *c) {
    return c->kind >= QA_CONTROLLER_AUTO && c->kind <= QA_CONTROLLER_SERIAL &&
           (c->kind < QA_CONTROLLER_GUID || (c->guid[32] == 0 && settings_guid(c->guid, false) &&
                                             (c->kind != QA_CONTROLLER_SERIAL || c->serial)));
}
static bool controller_read(const qa_json_document *d, qa_json_id id, qa_controller_selection *out,
                            qa_error *e) {
    unsigned kind;
    if (!settings_object(d, id, e) ||
        !settings_choice(d, qa_json_get(d, id, "kind"), selection_names, COUNT(selection_names),
                         &kind, e))
        return false;
    out->kind = (qa_controller_selection_kind)kind;
    if (kind < QA_CONTROLLER_GUID)
        return true;
    char *guid;
    if (!settings_string(d, qa_json_get(d, id, "guid"), &guid, e))
        return false;
    bool valid = settings_guid(guid, false);
    if (valid)
        memcpy(out->guid, guid, 33);
    free(guid);
    if (!valid)
        return settings_fail(e, "Controller GUID requires 32 hexadecimal digits");
    if (kind == QA_CONTROLLER_GUID) {
        uint32_t ordinal;
        if (!settings_u32(d, qa_json_get(d, id, "ordinal"), &ordinal, e))
            return false;
        out->ordinal = ordinal;
        return true;
    }
    char *serial;
    if (!settings_string(d, qa_json_get(d, id, "serial"), &serial, e))
        return false;
    out->serial = serial;
    return true;
}
static void controller_write(qa_json_writer *w, const qa_controller_selection *c) {
    qa_json_writer_object(w);
    settings_key_string(w, "kind", selection_names[c->kind]);
    if (c->kind >= QA_CONTROLLER_GUID)
        settings_key_string(w, "guid", c->guid);
    if (c->kind == QA_CONTROLLER_GUID)
        settings_key_number(w, "ordinal", c->ordinal);
    if (c->kind == QA_CONTROLLER_SERIAL)
        settings_key_string(w, "serial", c->serial);
    qa_json_writer_end(w);
}
void qa_seat_settings_free(qa_seat_settings *s) {
    if (!s)
        return;
    for (size_t i = 0; i < s->binding_count; ++i)
        free((void *)s->bindings[i].command);
    for (size_t i = 0; i < s->binding_default_count; ++i)
        free((void *)s->binding_defaults[i].command);
    for (size_t i = 0; i < s->history_count; ++i)
        free(s->history[i]);
    free(s->bindings);
    free(s->binding_defaults);
    free(s->binding_overrides);
    free(s->history);
    free((void *)s->controller.serial);
    *s = (qa_seat_settings){0};
}
bool qa_seat_settings_parse(qa_bytes bytes, qa_seat_settings *out, qa_error *e) {
    if (!out)
        return settings_fail(e, "Missing seat settings output");
    qa_json_document *d;
    if (!qa_json_parse(bytes, &d, e))
        return false;
    qa_json_id root = qa_json_root(d), bindings = qa_json_get(d, root, "bindings"),
               history = qa_json_get(d, root, "history");
    qa_seat_settings result = {.rumble_strength = 1};
    bool ok = settings_version(d, root, e);
    if (ok &&
        (qa_json_type(d, bindings) != QA_JSON_ARRAY || qa_json_type(d, history) != QA_JSON_ARRAY))
        ok = settings_fail(e, "Expected bindings/history lists");
    if (!ok)
        goto done;
    size_t nb = qa_json_size(d, bindings), nh = qa_json_size(d, history);
    if (nb > SIZE_MAX / sizeof(*result.bindings) || nh > SIZE_MAX / sizeof(*result.history)) {
        ok = settings_fail(e, "Settings arrays exceed native size");
        goto done;
    }
    result.bindings = nb ? calloc(nb, sizeof(*result.bindings)) : NULL;
    result.history = nh ? calloc(nh, sizeof(*result.history)) : NULL;
    if ((nb && !result.bindings) || (nh && !result.history)) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating seat settings");
        ok = false;
        goto done;
    }
    for (size_t i = 0; ok && i < nb; ++i) {
        ok = binding_read(d, qa_json_at(d, bindings, i), &result.bindings[i], e);
        if (ok)
            ++result.binding_count;
    }
    qa_json_id defaults = qa_json_get(d, root, "bindingDefaults"),
               overrides = qa_json_get(d, root, "bindingOverrides");
    result.has_binding_defaults = defaults != QA_JSON_NONE;
    if (ok && result.has_binding_defaults) {
        if (qa_json_type(d, defaults) != QA_JSON_ARRAY || qa_json_type(d, overrides) != QA_JSON_ARRAY)
            ok = settings_fail(e, "Expected binding provenance lists");
        size_t nd = qa_json_size(d, defaults), no = qa_json_size(d, overrides);
        if (ok && (nd > SIZE_MAX / sizeof(*result.binding_defaults) ||
                   no > SIZE_MAX / sizeof(*result.binding_overrides)))
            ok = settings_fail(e, "Binding provenance exceeds native size");
        if (ok) {
            result.binding_defaults = nd ? calloc(nd, sizeof(*result.binding_defaults)) : NULL;
            result.binding_overrides = no ? calloc(no, sizeof(*result.binding_overrides)) : NULL;
            if ((nd && !result.binding_defaults) || (no && !result.binding_overrides)) {
                qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating binding provenance"); ok = false;
            }
        }
        for (size_t i = 0; ok && i < nd; ++i) {
            ok = binding_read(d, qa_json_at(d, defaults, i), &result.binding_defaults[i], e);
            if (ok) ++result.binding_default_count;
        }
        for (size_t i = 0; ok && i < no; ++i) {
            ok = physical_read(d, qa_json_at(d, overrides, i), &result.binding_overrides[i], e);
            if (ok) ++result.binding_override_count;
        }
    } else if (ok && overrides != QA_JSON_NONE)
        ok = settings_fail(e, "Binding overrides require their default provenance");
    for (size_t i = 0; ok && i < nh; ++i) {
        ok = settings_string(d, qa_json_at(d, history, i), &result.history[i], e);
        if (ok)
            ++result.history_count;
    }
    if (!ok)
        goto done;
    qa_json_id always = qa_json_get(d, root, "alwaysRun"),
               strength = qa_json_get(d, root, "rumbleStrength");
    result.has_always_run = always != QA_JSON_NONE;
    ok = (!result.has_always_run || qa_json_bool(d, always, &result.always_run, e)) &&
         qa_json_bool(d, qa_json_get(d, root, "rumble"), &result.rumble, e) &&
         (strength == QA_JSON_NONE || settings_float(d, strength, &result.rumble_strength, e)) &&
         controller_read(d, qa_json_get(d, root, "controller"), &result.controller, e) &&
         gamepad_read(d, qa_json_get(d, root, "gamepad"), &result.gamepad, e) &&
         fields_read(d, qa_json_get(d, root, "mouse"), &result.mouse, mouse_fields,
                     COUNT(mouse_fields), e);
    if (ok && (result.rumble_strength < 0 || result.rumble_strength > 1))
        ok = settings_fail(e, "Vibration strength must be in [0,1]");
done:
    qa_json_destroy(d);
    if (ok)
        *out = result;
    else
        qa_seat_settings_free(&result);
    return ok;
}
bool qa_seat_settings_encode(const qa_seat_settings *s, qa_buffer *out, qa_error *e) {
    if (!s || !out || (s->binding_count && !s->bindings) || (s->history_count && !s->history) ||
        (s->binding_default_count && !s->binding_defaults) ||
        (s->binding_override_count && !s->binding_overrides) ||
        (!s->has_binding_defaults && (s->binding_default_count || s->binding_override_count)) ||
        !qa_gamepad_tuning_valid(&s->gamepad) || !qa_mouse_tuning_valid(&s->mouse) ||
        !controller_valid(&s->controller) || !isfinite(s->rumble_strength) ||
        s->rumble_strength < 0 || s->rumble_strength > 1)
        return settings_fail(e, "Invalid seat settings");
    for (size_t i = 0; i < s->binding_count; ++i)
        if (!binding_valid(&s->bindings[i]))
            return settings_fail(e, "Invalid input binding");
    for (size_t i = 0; i < s->binding_default_count; ++i)
        if (!binding_valid(&s->binding_defaults[i]))
            return settings_fail(e, "Invalid default binding provenance");
    for (size_t i = 0; i < s->binding_override_count; ++i)
        if (!qa_input_physical_valid(s->binding_overrides[i]))
            return settings_fail(e, "Invalid binding override provenance");
    qa_json_writer w = {0};
    qa_json_writer_object(&w);
    settings_key_number(&w, "version", 1);
    qa_json_writer_key(&w, "bindings");
    qa_json_writer_array(&w);
    for (size_t i = 0; i < s->binding_count; ++i)
        binding_write(&w, &s->bindings[i]);
    qa_json_writer_end(&w);
    if (s->has_binding_defaults) {
        qa_json_writer_key(&w, "bindingDefaults");
        qa_json_writer_array(&w);
        for (size_t i = 0; i < s->binding_default_count; ++i)
            binding_write(&w, &s->binding_defaults[i]);
        qa_json_writer_end(&w);
        qa_json_writer_key(&w, "bindingOverrides");
        qa_json_writer_array(&w);
        for (size_t i = 0; i < s->binding_override_count; ++i)
            physical_write(&w, &s->binding_overrides[i]);
        qa_json_writer_end(&w);
    }
    qa_json_writer_key(&w, "gamepad");
    gamepad_write(&w, &s->gamepad);
    qa_json_writer_key(&w, "mouse");
    qa_json_writer_object(&w);
    fields_write(&w, &s->mouse, mouse_fields, COUNT(mouse_fields));
    qa_json_writer_end(&w);
    qa_json_writer_key(&w, "history");
    qa_json_writer_array(&w);
    for (size_t i = 0; i < s->history_count; ++i)
        qa_json_writer_string(&w, s->history[i]);
    qa_json_writer_end(&w);
    if (s->has_always_run)
        settings_key_bool(&w, "alwaysRun", s->always_run);
    settings_key_bool(&w, "rumble", s->rumble);
    settings_key_number(&w, "rumbleStrength", s->rumble_strength);
    qa_json_writer_key(&w, "controller");
    controller_write(&w, &s->controller);
    qa_json_writer_end(&w);
    return settings_finish(&w, out, e);
}
void qa_gyro_profile_free(qa_gyro_profile *p) {
    if (p) {
        free((void *)p->serial);
        *p = (qa_gyro_profile){0};
    }
}
static bool gyro_valid(const qa_gyro_profile *p) {
    return p && (p->yaw_axis == QA_GYRO_YAW_Y || p->yaw_axis == QA_GYRO_YAW_Z) &&
           isfinite(p->yaw_sensitivity) && isfinite(p->pitch_sensitivity) &&
           (!p->device || (p->guid[32] == 0 && settings_guid(p->guid, true) && p->serial &&
                           *p->serial && strlen(p->serial) <= 256));
}
bool qa_gyro_profile_parse(qa_bytes bytes, qa_gyro_profile *out, qa_error *e) {
    if (!out || bytes.size > 8192)
        return settings_fail(e, "Invalid gyro profile size");
    qa_json_document *d;
    if (!qa_json_parse(bytes, &d, e))
        return false;
    qa_gyro_profile result = {0};
    qa_json_id root = qa_json_root(d), identity = qa_json_get(d, root, "identity"),
               tuning = qa_json_get(d, root, "tuning");
    bool ok = settings_version(d, root, e) && settings_object(d, identity, e) &&
              settings_object(d, tuning, e);
    if (!ok)
        goto done;
    if (qa_json_string_equal(d, qa_json_get(d, identity, "kind"), "device")) {
        char *guid = NULL, *serial = NULL;
        result.device = true;
        ok = settings_string(d, qa_json_get(d, identity, "guid"), &guid, e) &&
             settings_string(d, qa_json_get(d, identity, "serial"), &serial, e);
        if (ok) {
            ok = settings_guid(guid, true);
            if (ok)
                memcpy(result.guid, guid, 33);
            else
                settings_fail(e, "Invalid gyro device GUID");
        }
        result.serial = serial;
        free(guid);
    } else if (!qa_json_string_equal(d, qa_json_get(d, identity, "kind"), "seat"))
        ok = settings_fail(e, "Unknown gyro identity");
    unsigned axis;
    ok = ok && settings_choice(d, qa_json_get(d, tuning, "yawAxis"), yaw_axes, 2, &axis, e) &&
         settings_float(d, qa_json_get(d, tuning, "yawSensitivity"), &result.yaw_sensitivity, e) &&
         settings_float(d, qa_json_get(d, tuning, "pitchSensitivity"), &result.pitch_sensitivity,
                        e) &&
         qa_json_bool(d, qa_json_get(d, tuning, "enabled"), &result.enabled, e);
    if (ok) {
        result.yaw_axis = (qa_gyro_yaw_axis)axis;
        if (!gyro_valid(&result))
            ok = settings_fail(e, "Invalid gyro identity or tuning");
    }
done:
    qa_json_destroy(d);
    if (ok)
        *out = result;
    else
        qa_gyro_profile_free(&result);
    return ok;
}
bool qa_gyro_profile_encode(const qa_gyro_profile *p, qa_buffer *out, qa_error *e) {
    if (!out || !gyro_valid(p))
        return settings_fail(e, "Invalid gyro profile");
    qa_json_writer w = {0};
    qa_json_writer_object(&w);
    settings_key_number(&w, "version", 1);
    qa_json_writer_key(&w, "identity");
    qa_json_writer_object(&w);
    settings_key_string(&w, "kind", p->device ? "device" : "seat");
    if (p->device) {
        settings_key_string(&w, "guid", p->guid);
        settings_key_string(&w, "serial", p->serial);
    }
    qa_json_writer_end(&w);
    qa_json_writer_key(&w, "tuning");
    qa_json_writer_object(&w);
    settings_key_bool(&w, "enabled", p->enabled);
    settings_key_string(&w, "yawAxis", yaw_axes[p->yaw_axis]);
    settings_key_number(&w, "yawSensitivity", p->yaw_sensitivity);
    settings_key_number(&w, "pitchSensitivity", p->pitch_sensitivity);
    qa_json_writer_end(&w);
    qa_json_writer_end(&w);
    return settings_finish(&w, out, e);
}
