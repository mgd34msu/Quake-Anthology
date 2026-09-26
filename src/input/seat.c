#include "internal.h"
#include "qa/text.h"
#include <stdio.h>

bool qa_input_reserve(void **data, size_t *capacity, size_t needed, size_t stride,
                      qa_error *error) {
    if (needed <= *capacity)
        return true;
    size_t grown = *capacity ? *capacity : 16;
    while (grown < needed) {
        if (grown > SIZE_MAX / 2) {
            grown = needed;
            break;
        }
        grown *= 2;
    }
    if (grown > SIZE_MAX / stride) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Input storage overflow");
        return false;
    }
    void *next = realloc(*data, grown * stride);
    if (!next) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating input storage");
        return false;
    }
    *data = next;
    *capacity = grown;
    return true;
}
bool qa_input_text_append(qa_input_seat *s, const char *text, size_t size, qa_error *error) {
    if (size > SIZE_MAX - s->scratch_size - 1 ||
        !qa_input_reserve((void **)&s->scratch, &s->scratch_capacity, s->scratch_size + size + 1, 1,
                          error))
        return false;
    memcpy(s->scratch + s->scratch_size, text, size);
    s->scratch_size += size;
    s->scratch[s->scratch_size] = 0;
    return true;
}
bool qa_input_physical_equal(qa_physical_input a, qa_physical_input b) {
    return a.kind == b.kind && a.code == b.code &&
           (a.kind < QA_PHYSICAL_BUTTON || a.device == b.device) &&
           (a.kind != QA_PHYSICAL_AXIS || a.positive == b.positive);
}
uint64_t qa_input_physical_source(qa_physical_input input) {
    return ((uint64_t)input.kind << 61) | ((uint64_t)(uint32_t)input.device << 24) |
           ((uint64_t)input.code << 1) | (uint64_t)input.positive;
}
bool qa_input_command_source(qa_input_seat *s, const char *text, uint64_t *out, qa_error *error) {
    qa_string_id id;
    if (!qa_strings_intern_cstr(s->command_sources, text, &id, error))
        return false;
    *out = UINT64_C(0x8000000000000000) | id;
    return true;
}
static bool physical_valid(qa_physical_input input) {
    if (input.kind == QA_PHYSICAL_KEY)
        return input.code <= UINT16_MAX;
    if (input.kind == QA_PHYSICAL_MOUSE)
        return input.code > 0 && input.code <= 255;
    if (input.kind == QA_PHYSICAL_BUTTON)
        return input.device >= 0 && input.code <= 255;
    return input.kind == QA_PHYSICAL_AXIS && input.device >= 0 && input.code < QA_AXIS_COUNT;
}
static void binding_release(qa_binding_record *binding) {
    if (binding && --binding->references == 0)
        free(binding);
}
static qa_binding_record *binding_find(const qa_input_seat *s, qa_physical_input input,
                                       size_t *index) {
    for (size_t i = 0; i < s->binding_count; ++i)
        if (qa_input_physical_equal(s->bindings[i]->view.input, input)) {
            if (index)
                *index = i;
            return s->bindings[i];
        }
    return NULL;
}
qa_input_seat *qa_input_seat_create(const qa_input_seat_options *o, qa_error *error) {
    if (!o || !o->console || o->context.origin != QA_COMMAND_SEAT || o->context.seat >= 4 ||
        o->context.dialect < QA_CONSOLE_Q1 || o->context.dialect > QA_CONSOLE_Q3 ||
        !qa_gamepad_tuning_valid(&o->gamepad)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid local input seat");
        return NULL;
    }
    qa_input_seat *s = calloc(1, sizeof(*s));
    if (!s) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating input seat");
        return NULL;
    }
    s->options = *o;
    s->focused = true;
    s->next_ui = 1;
    if (!qa_strings_create(&s->command_sources, error) ||
        !qa_input_reserve((void **)&s->ui, &s->ui_capacity, 1, sizeof(*s->ui), error)) {
        qa_input_seat_destroy(s);
        return NULL;
    }
    s->ui[s->ui_count++] = (qa_ui_record){.handler = o->ui, .user = o->ui_user, .active = true};
    return s;
}
void qa_input_seat_destroy(qa_input_seat *s) {
    if (!s)
        return;
    /* The owner releases input while its console is still alive. Destruction
     * then has no callbacks and remains safe during failed construction. */
    for (size_t i = 0; i < s->binding_count; ++i)
        binding_release(s->bindings[i]);
    for (size_t i = 0; i < s->held_count; ++i)
        binding_release(s->held[i].binding);
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
        qa_input_button_destroy(&s->buttons[i]);
    qa_strings_destroy(s->command_sources);
    free(s->bindings);
    free(s->held);
    free(s->ui);
    free(s->scratch);
    free(s);
}
qa_command_context qa_input_seat_context(const qa_input_seat *s) { return s->options.context; }
qa_input_focus qa_input_seat_focus(const qa_input_seat *s) { return s->focus; }
bool qa_input_seat_focused(const qa_input_seat *s) { return s->focused; }
bool qa_input_seat_has_held(const qa_input_seat *s) {
    if (s->held_count)
        return true;
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
        if (s->buttons[i].count)
            return true;
    return false;
}
qa_gamepad_input *qa_input_seat_gamepad(qa_input_seat *s) { return &s->gamepad; }
qa_gamepad_tuning *qa_input_seat_gamepad_tuning(qa_input_seat *s) { return &s->options.gamepad; }
bool qa_input_seat_profile(qa_input_seat *s, qa_console_dialect dialect, qa_error *error) {
    if (dialect < QA_CONSOLE_Q1 || dialect > QA_CONSOLE_Q3 || qa_input_seat_has_held(s)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "Input profile requires valid dialect and released keys");
        return false;
    }
    s->options.context.dialect = dialect;
    return true;
}
bool qa_input_seat_bind(qa_input_seat *s, const qa_input_binding *binding, qa_error *error) {
    if (!s || !binding || !physical_valid(binding->input) ||
        (binding->kind != QA_BIND_ACTION && binding->kind != QA_BIND_COMMAND) ||
        (binding->kind == QA_BIND_ACTION &&
         (binding->action < 0 || binding->action >= QA_INPUT_ACTION_COUNT)) ||
        (binding->kind == QA_BIND_COMMAND && !binding->command)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input binding");
        return false;
    }
    size_t n = binding->kind == QA_BIND_COMMAND ? strlen(binding->command) : 0;
    if (n > SIZE_MAX - sizeof(qa_binding_record) - 1) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Binding length overflow");
        return false;
    }
    qa_binding_record *record = malloc(sizeof(*record) + n + 1);
    if (!record) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating key binding");
        return false;
    }
    *record = (qa_binding_record){.view = *binding, .references = 1};
    record->text[0] = 0;
    if (binding->kind == QA_BIND_COMMAND) {
        memcpy(record->text, binding->command, n + 1);
        record->view.command = record->text;
    } else
        record->view.command = NULL;
    size_t index = 0;
    qa_binding_record *old = binding_find(s, binding->input, &index);
    if (old) {
        s->bindings[index] = record;
        binding_release(old);
        return true;
    }
    if (!qa_input_reserve((void **)&s->bindings, &s->binding_capacity, s->binding_count + 1,
                          sizeof(*s->bindings), error)) {
        free(record);
        return false;
    }
    s->bindings[s->binding_count++] = record;
    return true;
}
bool qa_input_seat_unbind(qa_input_seat *s, qa_physical_input input) {
    size_t index = 0;
    qa_binding_record *old = binding_find(s, input, &index);
    if (!old)
        return false;
    memmove(s->bindings + index, s->bindings + index + 1,
            (s->binding_count - index - 1) * sizeof(*s->bindings));
    --s->binding_count;
    binding_release(old);
    return true;
}
void qa_input_seat_unbind_all(qa_input_seat *s) {
    for (size_t i = 0; i < s->binding_count; ++i)
        binding_release(s->bindings[i]);
    s->binding_count = 0;
}
size_t qa_input_seat_binding_count(const qa_input_seat *s) { return s->binding_count; }
const qa_input_binding *qa_input_seat_binding_at(const qa_input_seat *s, size_t i) {
    return i < s->binding_count ? &s->bindings[i]->view : NULL;
}
const qa_input_binding *qa_input_seat_binding(const qa_input_seat *s, qa_physical_input input) {
    qa_binding_record *b = binding_find(s, input, NULL);
    return b ? &b->view : NULL;
}
bool qa_input_seat_remap_controller(qa_input_seat *s, int32_t device, qa_error *error) {
    if (device < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Negative controller instance");
        return false;
    }
    /* Clone into a candidate list so failure and duplicate remaps are atomic. */
    qa_input_seat candidate = {0};
    for (size_t i = 0; i < s->binding_count; ++i) {
        qa_input_binding b = s->bindings[i]->view;
        if (b.input.kind >= QA_PHYSICAL_BUTTON)
            b.input.device = device;
        if (!qa_input_seat_bind(&candidate, &b, error)) {
            qa_input_seat_unbind_all(&candidate);
            free(candidate.bindings);
            return false;
        }
    }
    qa_input_seat_unbind_all(s);
    free(s->bindings);
    s->bindings = candidate.bindings;
    s->binding_count = candidate.binding_count;
    s->binding_capacity = candidate.binding_capacity;
    return true;
}
bool qa_input_seat_action(qa_input_seat *s, qa_input_action action, uint64_t source, bool down,
                          double time, qa_error *error) {
    if (!s || action < 0 || action >= QA_INPUT_ACTION_COUNT || !isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input action");
        return false;
    }
    if (down)
        return qa_input_button_down(&s->buttons[action], source, time, error);
    qa_input_button_up(&s->buttons[action], source, time, 10);
    return true;
}
static uint64_t command_key(qa_physical_input p) {
    if (p.kind == QA_PHYSICAL_KEY)
        return p.code;
    if (p.kind == QA_PHYSICAL_MOUSE)
        return QA_KEY_MOUSE1 + qa_input_mouse_button(p.code) - 1;
    return UINT64_C(65536) + (uint64_t)(uint32_t)p.device * 64 +
           (p.kind == QA_PHYSICAL_BUTTON ? p.code : 32 + p.code * 2 + (unsigned)p.positive);
}
static bool run_binding(qa_input_seat *s, const qa_held_binding *held, bool down, double time,
                        qa_error *error) {
    const qa_input_binding *b = held->binding ? &held->binding->view : NULL;
    if (!b)
        return true;
    if (b->kind == QA_BIND_ACTION)
        return qa_input_seat_action(s, b->action, qa_input_physical_source(held->input), down, time,
                                    error);
    const char *text = b->command;
    size_t left = strlen(text);
    bool had_button = false;
    s->scratch_size = 0;
    while (left) {
        size_t n = qa_command_separator(text, left, s->options.context.dialect),
               consumed = n < left ? n + 1 : n;
        const char *segment = text;
        size_t size = n;
        while (size && (unsigned char)*segment <= 32) {
            ++segment;
            --size;
        }
        while (size && (unsigned char)segment[size - 1] <= 32)
            --size;
        if (size && *segment == '+') {
            char tail[96];
            int written = snprintf(tail, sizeof(tail), " %llu %.0f\n",
                                   (unsigned long long)command_key(held->input), trunc(time));
            if (written < 0 || (size_t)written >= sizeof(tail) ||
                !qa_input_text_append(s, down ? "+" : "-", 1, error) ||
                !qa_input_text_append(s, segment + 1, size - 1, error) ||
                !qa_input_text_append(s, tail, (size_t)written, error))
                return false;
            had_button = true;
        } else if (size && (down || had_button)) {
            if (!qa_input_text_append(s, segment, size, error) ||
                !qa_input_text_append(s, "\n", 1, error))
                return false;
        }
        text += consumed;
        left -= consumed;
    }
    if (!s->scratch_size)
        return true;
    qa_command_context context = s->options.context;
    context.script = "key-binding";
    context.direct = false;
    return qa_console_append(s->options.console, &context, s->scratch, error);
}
static bool digital(qa_input_seat *s, qa_physical_input input, bool down, double time,
                    bool consumed, qa_error *error) {
    size_t index = 0;
    while (index < s->held_count && !qa_input_physical_equal(s->held[index].input, input))
        ++index;
    if (down) {
        if (index < s->held_count)
            return true;
        if (!qa_input_reserve((void **)&s->held, &s->held_capacity, s->held_count + 1,
                              sizeof(*s->held), error))
            return false;
        qa_binding_record *binding = !consumed && s->focus == QA_INPUT_GAME && s->focused
                                         ? binding_find(s, input, NULL)
                                         : NULL;
        qa_held_binding held = {input, binding};
        if (binding)
            ++binding->references;
        if (!run_binding(s, &held, true, time, error)) {
            binding_release(binding);
            return false;
        }
        s->held[s->held_count++] = held;
        return true;
    }
    if (index == s->held_count)
        return true;
    if (!run_binding(s, &s->held[index], false, time, error))
        return false;
    binding_release(s->held[index].binding);
    memmove(s->held + index, s->held + index + 1, (s->held_count - index - 1) * sizeof(*s->held));
    --s->held_count;
    return true;
}
bool qa_input_seat_release_device(qa_input_seat *s, int32_t device, double time, qa_error *error) {
    bool success = true;
    for (size_t i = 0; i < s->held_count;) {
        qa_held_binding *held = &s->held[i];
        if (held->input.kind < QA_PHYSICAL_BUTTON || held->input.device != device) {
            ++i;
            continue;
        }
        if (!run_binding(s, held, false, time, error))
            success = false;
        binding_release(held->binding);
        memmove(held, held + 1, (s->held_count - i - 1) * sizeof(*held));
        --s->held_count;
    }
    qa_gamepad_clear(&s->gamepad);
    qa_gamepad_calibration_reset(&s->gamepad);
    return success;
}
bool qa_input_seat_release(qa_input_seat *s, double time, qa_error *error) {
    if (!isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input release time");
        return false;
    }
    bool success = true;
    for (size_t i = 0; i < s->held_count; ++i) {
        if (!run_binding(s, &s->held[i], false, time, error))
            success = false;
        binding_release(s->held[i].binding);
    }
    s->held_count = 0;
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i)
        qa_input_button_release(&s->buttons[i], time);
    qa_gamepad_clear(&s->gamepad);
    s->mouse = (qa_input_pair){0};
    s->impulse = 0;
    return success;
}
bool qa_input_seat_set_focus(qa_input_seat *s, qa_input_focus focus, double time, qa_error *error) {
    if (focus < QA_INPUT_GAME || focus > QA_INPUT_UI) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid seat focus");
        return false;
    }
    bool result = qa_input_seat_release(s, time, error);
    s->focus = focus;
    return result;
}
bool qa_input_seat_ui_push(qa_input_seat *s, qa_input_ui_handler handler, void *user, double time,
                           qa_input_ui_token *out, qa_error *error) {
    if (!handler || !out || s->next_ui == UINT64_MAX ||
        !qa_input_reserve((void **)&s->ui, &s->ui_capacity, s->ui_count + 1, sizeof(*s->ui), error))
        return false;
    if (!qa_input_seat_release(s, time, error))
        return false;
    s->ui[s->ui_count - 1].focus = s->focus;
    *out = s->next_ui++;
    s->ui[s->ui_count++] = (qa_ui_record){
        .id = *out, .handler = handler, .user = user, .focus = s->focus, .active = true};
    return true;
}
bool qa_input_seat_ui_remove(qa_input_seat *s, qa_input_ui_token id, double time, qa_error *error) {
    if (!id)
        return true;
    for (size_t i = 1; i < s->ui_count; ++i)
        if (s->ui[i].id == id && s->ui[i].active) {
            s->ui[i].active = false;
            if (i + 1 != s->ui_count)
                return true;
            bool success = qa_input_seat_release(s, time, error);
            while (s->ui_count > 1 && !s->ui[s->ui_count - 1].active)
                --s->ui_count;
            s->focus = s->ui[s->ui_count - 1].focus;
            return success;
        }
    return true;
}
bool qa_input_seat_event(qa_input_seat *s, const qa_input_event *event, bool *consumed,
                         qa_error *error) {
    if (!s || !event || !isfinite(event->time_ms) || event->time_ms < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input event");
        return false;
    }
    if (consumed)
        *consumed = false;
    bool released = true;
    if (event->kind == QA_INPUT_EVENT_FOCUS) {
        s->focused = event->down;
        if (!s->focused)
            released = qa_input_seat_release(s, event->time_ms, error);
    } else if (!s->focused)
        return true;
    qa_ui_record ui = s->ui[s->ui_count - 1];
    bool used = ui.handler && ui.handler(ui.user, s, s->focus, event);
    if (consumed)
        *consumed = used || s->focus == QA_INPUT_GAME;
    if (!released)
        return false;
    switch (event->kind) {
    case QA_INPUT_EVENT_KEY:
    case QA_INPUT_EVENT_BUTTON:
        if (!physical_valid(event->input) ||
            (event->kind == QA_INPUT_EVENT_KEY ? event->input.kind != QA_PHYSICAL_KEY
                                               : event->input.kind != QA_PHYSICAL_MOUSE &&
                                                     event->input.kind != QA_PHYSICAL_BUTTON)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid physical input");
            return false;
        }
        return digital(s, event->input, event->down, event->time_ms, used, error);
    case QA_INPUT_EVENT_AXIS:
        if (!physical_valid(event->input) || event->input.kind != QA_PHYSICAL_AXIS)
            return false;
        if (!qa_gamepad_axis(&s->gamepad, (qa_controller_axis)event->input.code, event->value,
                             !used && s->focus == QA_INPUT_GAME, error))
            return false;
        for (unsigned side = 0; side < 2; ++side) {
            qa_physical_input p = event->input;
            p.positive = side != 0;
            float value = p.positive ? event->value : -event->value;
            if (!digital(s, p, value > s->options.gamepad.trigger_threshold, event->time_ms, used,
                         error))
                return false;
        }
        return true;
    case QA_INPUT_EVENT_MOUSE:
        if (!isfinite(event->delta.x) || !isfinite(event->delta.y)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid pointer displacement");
            return false;
        }
        if (!used && s->focus == QA_INPUT_GAME) {
            qa_input_pair next = {s->mouse.x + event->delta.x, s->mouse.y + event->delta.y};
            if (!isfinite(next.x) || !isfinite(next.y)) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Pointer displacement overflow");
                return false;
            }
            s->mouse = next;
        }
        return true;
    case QA_INPUT_EVENT_WHEEL: {
        if (!isfinite(event->delta.y) || fabsf(event->delta.y) > (float)UINT16_MAX) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid wheel displacement");
            return false;
        }
        qa_physical_input p = {.kind = QA_PHYSICAL_KEY,
                               .code = event->delta.y > 0 ? QA_KEY_WHEEL_UP : QA_KEY_WHEEL_DOWN};
        unsigned count = (unsigned)ceilf(fabsf(event->delta.y));
        for (unsigned i = 0; i < count; ++i)
            if (!digital(s, p, true, event->time_ms, used, error) ||
                !digital(s, p, false, event->time_ms, false, error))
                return false;
        return true;
    }
    case QA_INPUT_EVENT_TEXT:
    case QA_INPUT_EVENT_FOCUS:
    case QA_INPUT_EVENT_TOUCH:
        return true;
    default:
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Unknown input event");
        return false;
    }
}
bool qa_input_seat_sample(qa_input_seat *s, double now, double frame, qa_seat_input_sample *out,
                          qa_error *error) {
    if (!out || !isfinite(now) || now < 0 || !isfinite(frame) || frame <= 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid seat sample");
        return false;
    }
    qa_seat_input_sample result = {.mouse = s->mouse,
                                   .frame_ms = frame,
                                   .game_focus = s->focus == QA_INPUT_GAME,
                                   .any_key_down = s->held_count != 0,
                                   .impulse = s->impulse};
    if (!qa_gamepad_sample_read(&s->gamepad, &s->options.gamepad, frame, &result.gamepad, error))
        return false;
    qa_console_dialect dialect = s->options.context.dialect;
    qa_button_timing timing = dialect <= QA_CONSOLE_QW   ? QA_BUTTON_Q1
                              : dialect == QA_CONSOLE_Q3 ? QA_BUTTON_Q3
                                                         : QA_BUTTON_Q2;
    for (size_t i = 0; i < QA_INPUT_ACTION_COUNT; ++i) {
        result.buttons[i].active = s->buttons[i].count != 0;
        result.buttons[i].pressed = s->buttons[i].pressed;
        if (!qa_input_button_sample(&s->buttons[i], timing, now, frame, &result.buttons[i].fraction,
                                    error))
            return false;
    }
    s->mouse = (qa_input_pair){0};
    s->impulse = 0;
    *out = result;
    return true;
}
bool qa_input_seat_impulse(qa_input_seat *s, const char *text, qa_error *error) {
    if (!text) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing impulse");
        return false;
    }
    qa_console_dialect d = s->options.context.dialect;
    if (d == QA_CONSOLE_Q3 || d == QA_CONSOLE_Q2_RERELEASE) {
        const char *start = text;
        while (*start && (unsigned char)*start <= 32)
            ++start;
        double value = 0;
        if ((*start &&
             !qa_parse_number((qa_bytes){(const uint8_t *)start, strlen(start)}, &value, error)) ||
            !isfinite(value) || value < 0 || value > 255 || value != trunc(value)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Impulse must fit an unsigned byte");
            return false;
        }
        s->impulse = (uint8_t)value;
        return true;
    }
    const unsigned char *p = (const unsigned char *)text;
    bool negative = false;
    if (d == QA_CONSOLE_Q2)
        while (*p && *p <= 32)
            ++p;
    if (*p == '-' || (d == QA_CONSOLE_Q2 && *p == '+'))
        negative = *p++ == '-';
    unsigned base = 10, value = 0;
    if (d <= QA_CONSOLE_QW && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (d <= QA_CONSOLE_QW && p[0] == '\'') {
        s->impulse = (uint8_t)(negative ? 0u - p[1] : p[1]);
        return true;
    }
    for (; *p; ++p) {
        unsigned digit = *p >= '0' && *p <= '9'   ? (unsigned)(*p - '0')
                         : *p >= 'a' && *p <= 'f' ? (unsigned)(*p - 'a' + 10)
                         : *p >= 'A' && *p <= 'F' ? (unsigned)(*p - 'A' + 10)
                                                  : base;
        if (digit >= base)
            break;
        value = (value * base + digit) & 255;
    }
    s->impulse = (uint8_t)(negative ? 0u - value : value);
    return true;
}
