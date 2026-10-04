#include "internal.h"
#include "qa/text.h"
#include <stdio.h>

static const char *const actions[QA_INPUT_ACTION_COUNT] = {
    [QA_INPUT_ATTACK] = "+attack", [QA_INPUT_JUMP] = "+jump",
    [QA_INPUT_FORWARD] = "+forward", [QA_INPUT_BACK] = "+back",
    [QA_INPUT_MOVE_LEFT] = "+moveleft", [QA_INPUT_MOVE_RIGHT] = "+moveright",
    [QA_INPUT_MOVE_UP] = "+moveup", [QA_INPUT_MOVE_DOWN] = "+movedown",
    [QA_INPUT_USE] = "+use", [QA_INPUT_CROUCH] = "+crouch",
    [QA_INPUT_WALK] = "+speed", [QA_INPUT_SCORES] = "+scores",
    [QA_INPUT_TURN_LEFT] = "+left", [QA_INPUT_TURN_RIGHT] = "+right",
    [QA_INPUT_LOOK_UP] = "+lookup", [QA_INPUT_LOOK_DOWN] = "+lookdown",
    [QA_INPUT_STRAFE] = "+strafe", [QA_INPUT_MLOOK] = "+mlook",
    [QA_INPUT_KLOOK] = "+klook", [QA_INPUT_HOLSTER] = "+holster",
    [QA_INPUT_BUTTON0] = "+button0", [QA_INPUT_BUTTON1] = "+button1",
    [QA_INPUT_BUTTON2] = "+button2", [QA_INPUT_BUTTON3] = "+button3",
    [QA_INPUT_BUTTON4] = "+button4", [QA_INPUT_BUTTON5] = "+button5",
    [QA_INPUT_BUTTON6] = "+button6", [QA_INPUT_BUTTON7] = "+button7",
    [QA_INPUT_BUTTON8] = "+button8", [QA_INPUT_BUTTON9] = "+button9",
    [QA_INPUT_BUTTON10] = "+button10", [QA_INPUT_BUTTON11] = "+button11",
    [QA_INPUT_BUTTON12] = "+button12", [QA_INPUT_BUTTON13] = "+button13",
    [QA_INPUT_BUTTON14] = "+button14"
};
const char *qa_input_action_command(qa_input_action action) {
    return (unsigned)action < QA_INPUT_ACTION_COUNT ? actions[action] : NULL;
}
struct qa_input_console {
    qa_input_console_options options;
    char names[96][32];
    size_t count;
};
static void print(qa_input_console *c, const char *text) {
    if (c->options.print)
        c->options.print(c->options.user, text);
}
static bool command(void *user, const qa_command_invocation *cmd, qa_error *error) {
    qa_input_console *c = user;
    if (cmd->context.origin != QA_COMMAND_SEAT && cmd->context.origin != QA_COMMAND_LOCAL)
        return true;
    qa_input_seat *s = c->options.seat(c->options.user, &cmd->context);
    if (!cmd->argc)
        return true;
    const char *name = cmd->argv[0];
    if ((*name == '+' || *name == '-') && c->options.scores &&
        qa_input_ascii_equal(name + 1, "scores") && c->options.scores(c->options.user, cmd))
        return true;
    if (!s)
        return true;
    if (*name == '+' || *name == '-') {
        bool down = *name == '+';
        const char *base = name + 1;
        if (qa_input_ascii_equal(base, "weaponwheel") || qa_input_ascii_equal(base, "wheel") ||
            qa_input_ascii_equal(base, "powerupwheel") || qa_input_ascii_equal(base, "wheel2")) {
            if (c->options.wheel)
                c->options.wheel(c->options.user, s,
                                 qa_input_ascii_equal(base, "powerupwheel") ||
                                     qa_input_ascii_equal(base, "wheel2"),
                                 down);
            return true;
        }
        qa_input_action action = QA_INPUT_ACTION_COUNT;
        if (qa_input_ascii_equal(base, "showscores"))
            action = QA_INPUT_SCORES;
        for (unsigned i = 0; action == QA_INPUT_ACTION_COUNT && i < QA_INPUT_ACTION_COUNT; ++i)
            if (actions[i] && (i < QA_INPUT_BUTTON0 || i > QA_INPUT_BUTTON14) &&
                qa_input_ascii_equal(base, actions[i] + 1))
                action = (qa_input_action)i;
        if (action == QA_INPUT_ACTION_COUNT && strncmp(base, "button", 6) == 0) {
            char *end;
            long number = strtol(base + 6, &end, 10);
            if (end != base + 6 && !*end && number >= 0 && number < 15)
                action = (qa_input_action)((int)QA_INPUT_BUTTON0 + (int)number);
        }
        if (action == QA_INPUT_ACTION_COUNT)
            return true;
        double time = 0;
        if (cmd->argc > 2 &&
            (!qa_parse_number((qa_bytes){(const uint8_t *)cmd->argv[2], strlen(cmd->argv[2])},
                              &time, error) ||
             !isfinite(time) || time < 0))
            return false;
        if (!down && cmd->argc < 2) {
            qa_input_button_release(&s->buttons[action], time);
            return true;
        }
        uint64_t source;
        if (!qa_input_command_source(s, cmd->argc > 1 ? cmd->argv[1] : "console", &source, error))
            return false;
        return qa_input_seat_action(s, action, source, down, time, error);
    }
    if (qa_input_ascii_equal(name, "impulse"))
        return qa_input_seat_impulse(s, cmd->argc > 1 ? cmd->argv[1] : "", error);
    if (qa_input_ascii_equal(name, "centerview")) {
        if (c->options.center)
            c->options.center(c->options.user, s);
        return true;
    }
    if (qa_input_ascii_equal(name, "unbindall")) {
        qa_input_seat_unbind_all(s);
        return true;
    }
    if (qa_input_ascii_equal(name, "bindlist")) {
        for (size_t i = 0; i < s->binding_count; ++i) {
            char physical[128], line[256];
            const qa_input_binding *b = &s->bindings[i]->view;
            if (!qa_input_physical_name(b->input, physical, sizeof(physical)))
                continue;
            (void)snprintf(line, sizeof(line), "%s = ", physical);
            print(c, line);
            if (b->kind == QA_BIND_COMMAND)
                print(c, b->command);
            else {
                (void)snprintf(line, sizeof(line), "action %u", (unsigned)b->action);
                print(c, line);
            }
            print(c, "\n");
        }
        return true;
    }
    if (cmd->argc < 2) {
        print(c, qa_input_ascii_equal(name, "bind") ? "bind <key> [command]\n" : "unbind <key>\n");
        return true;
    }
    int32_t device = 0;
    for (size_t i = 0; i < s->binding_count; ++i)
        if (s->bindings[i]->view.input.kind >= QA_PHYSICAL_BUTTON) {
            device = s->bindings[i]->view.input.device;
            break;
        }
    qa_physical_input physical;
    if (!qa_input_physical_parse(cmd->argv[1], device, &physical)) {
        print(c, "Unknown key\n");
        return true;
    }
    if (qa_input_ascii_equal(name, "unbind")) {
        if (physical.kind >= QA_PHYSICAL_BUTTON) {
            for (size_t i = 0; i < s->binding_count;) {
                qa_physical_input p = s->bindings[i]->view.input;
                p.device = physical.device;
                if (qa_input_physical_equal(p, physical))
                    qa_input_seat_unbind(s, s->bindings[i]->view.input);
                else
                    ++i;
            }
        } else
            qa_input_seat_unbind(s, physical);
        return true;
    }
    if (cmd->argc == 2) {
        const qa_input_binding *b = qa_input_seat_binding(s, physical);
        print(c, b && b->kind == QA_BIND_COMMAND ? b->command : b ? "Action binding" : "Unbound");
        print(c, "\n");
        return true;
    }
    s->scratch_size = 0;
    for (size_t i = 2; i < cmd->argc; ++i)
        if ((i > 2 && !qa_input_text_append(s, " ", 1, error)) ||
            !qa_input_text_append(s, cmd->argv[i], strlen(cmd->argv[i]), error))
            return false;
    qa_input_binding binding = {.input = physical, .kind = QA_BIND_COMMAND, .command = s->scratch};
    return qa_input_seat_bind(s, &binding, error);
}
static bool register_command(qa_input_console *c, const char *name, qa_error *error) {
    if (qa_console_find(c->options.console, NULL, name))
        return true;
    if (c->count >= sizeof(c->names) / sizeof(*c->names) || strlen(name) >= sizeof(c->names[0]))
        return false;
    if (!qa_console_register_owned(c->options.console, name, "Local seat input", 0, c->options.owner, true,
                             command, c, error))
        return false;
    (void)snprintf(c->names[c->count++], sizeof(c->names[0]), "%s", name);
    return true;
}
qa_input_console *qa_input_console_create(const qa_input_console_options *o, qa_error *error) {
    if (!o || !o->console || !o->seat) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid input console owner");
        return NULL;
    }
    qa_input_console *c = calloc(1, sizeof(*c));
    if (!c) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating input console");
        return NULL;
    }
    c->options = *o;
    char name[32];
    static const qa_input_action order[] = {
        QA_INPUT_ATTACK, QA_INPUT_JUMP, QA_INPUT_FORWARD, QA_INPUT_BACK,
        QA_INPUT_MOVE_LEFT, QA_INPUT_MOVE_RIGHT, QA_INPUT_MOVE_UP, QA_INPUT_MOVE_DOWN,
        QA_INPUT_USE, QA_INPUT_CROUCH, QA_INPUT_WALK, QA_INPUT_SCORES,
        QA_INPUT_TURN_LEFT, QA_INPUT_TURN_RIGHT, QA_INPUT_LOOK_UP, QA_INPUT_LOOK_DOWN,
        QA_INPUT_STRAFE, QA_INPUT_MLOOK, QA_INPUT_KLOOK, QA_INPUT_HOLSTER};
    for (size_t i = 0; i < sizeof(order) / sizeof(*order); ++i) {
        for (unsigned down = 0; down < 2; ++down) {
            (void)snprintf(name, sizeof(name), "%c%s", down ? '+' : '-', actions[order[i]] + 1);
            if (!register_command(c, name, error))
                goto fail;
        }
        if (order[i] == QA_INPUT_SCORES)
            for (unsigned down = 0; down < 2; ++down) {
                (void)snprintf(name, sizeof(name), "%cshowscores", down ? '+' : '-');
                if (!register_command(c, name, error))
                    goto fail;
            }
    }
    for (unsigned i = 0; i < 15; ++i)
        for (unsigned down = 0; down < 2; ++down) {
            (void)snprintf(name, sizeof(name), "%c%s", down ? '+' : '-', actions[QA_INPUT_BUTTON0 + i] + 1);
            if (!register_command(c, name, error))
                goto fail;
        }
    static const char *const wheels[] = {"weaponwheel", "wheel", "powerupwheel", "wheel2"};
    for (size_t i = 0; i < 4; ++i)
        for (unsigned down = 0; down < 2; ++down) {
            (void)snprintf(name, sizeof(name), "%c%s", down ? '+' : '-', wheels[i]);
            if (!register_command(c, name, error))
                goto fail;
        }
    static const char *const plain[] = {"impulse",   "bind",     "unbind",
                                        "unbindall", "bindlist", "centerview"};
    for (size_t i = 0; i < sizeof(plain) / sizeof(*plain); ++i)
        if (!register_command(c, plain[i], error))
            goto fail;
    return c;
fail:
    qa_input_console_destroy(c);
    return NULL;
}
void qa_input_console_destroy(qa_input_console *c) {
    if (!c)
        return;
    for (size_t i = 0; i < c->count; ++i)
        qa_console_unregister(c->options.console, c->names[i], 0);
    free(c);
}
static const char *const default_rows[][2] = {{"w", "+forward"},
                                          {"s", "+back"},
                                          {"a", "+moveleft"},
                                          {"d", "+moveright"},
                                          {"SPACE", "+moveup"},
                                          {"CTRL", "+movedown"},
                                          {"SHIFT", "+speed"},
                                          {"MOUSE1", "+attack"},
                                          {"TAB", "+scores"},
                                          {"MWHEELUP", "weapprev"},
                                          {"MWHEELDOWN", "weapnext"},
                                          {"q", "+weaponwheel"},
                                          {"GAMEPAD_RIGHT_TRIGGER", "+attack"},
                                          {"GAMEPAD_A_BUTTON", "+moveup"},
                                          {"GAMEPAD_B_BUTTON", "+movedown"},
                                          {"GAMEPAD_X_BUTTON", "+use"},
                                          {"GAMEPAD_LEFT_SHOULDER", "weapprev"},
                                          {"GAMEPAD_RIGHT_SHOULDER", "weapnext"},
                                          {"GAMEPAD_BACK", "+scores"}};
bool qa_input_default_binding_at(qa_console_dialect dialect, int32_t device, size_t index,
                                  qa_input_binding *out) {
    if (!out || (unsigned)dialect > QA_CONSOLE_Q3 || device < 0 ||
        index >= sizeof(default_rows) / sizeof(*default_rows)) return false;
    qa_input_binding binding = {.kind = QA_BIND_COMMAND, .command = default_rows[index][1]};
    if (dialect <= QA_CONSOLE_QW && (index == 4 || index == 13)) binding.command = "+jump";
    if (!qa_input_physical_parse(default_rows[index][0], device, &binding.input)) return false;
    *out = binding; return true;
}
static bool default_bindings(qa_input_seat *s, int32_t device, bool replace, qa_error *error) {
    if (!s) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Default bindings require an input seat"); return false; }
    qa_input_binding bindings[sizeof(default_rows) / sizeof(*default_rows)];
    for (size_t i = 0; i < sizeof(default_rows) / sizeof(*default_rows); ++i) {
        if (!qa_input_default_binding_at(s->options.context.dialect, device, i, bindings + i)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Default key name is invalid"); return false;
        }
    }
    if (replace) return qa_input_seat_replace_bindings(s, bindings, sizeof(bindings) / sizeof(*bindings), error);
    for (size_t i = 0; i < sizeof(bindings) / sizeof(*bindings); ++i)
        if (!qa_input_seat_bind(s, &bindings[i], error)) return false;
    return true;
}
bool qa_input_default_bindings(qa_input_seat *s, int32_t device, qa_error *error) {
    return default_bindings(s, device, false, error);
}
bool qa_input_reset_default_bindings(qa_input_seat *s, int32_t device, qa_error *error) {
    return default_bindings(s, device, true, error);
}
bool qa_input_bindings_config(const qa_input_seat *s, bool controllers, qa_buffer *out,
                              qa_error *error) {
    qa_input_seat text = {0};
    bool success = qa_input_text_append(&text, "unbindall\n", 10, error);
    static const char *const pads[] = {
        "A_BUTTON",  "B_BUTTON",   "X_BUTTON",    "Y_BUTTON",      "BACK",           "GUIDE",
        "START",     "LEFT_STICK", "RIGHT_STICK", "LEFT_SHOULDER", "RIGHT_SHOULDER", "DPAD_UP",
        "DPAD_DOWN", "DPAD_LEFT",  "DPAD_RIGHT",  "MISC",          "PADDLE1",        "PADDLE2",
        "PADDLE3",   "PADDLE4",    "TOUCHPAD"};
    for (size_t i = 0; success && i < s->binding_count; ++i) {
        const qa_input_binding *b = &s->bindings[i]->view;
        char key[96];
        const char *bound_command = b->kind == QA_BIND_COMMAND ? b->command : qa_input_action_command(b->action);
        if (!bound_command) continue;
        if (b->input.kind < QA_PHYSICAL_BUTTON) {
            if (!qa_input_physical_name(b->input, key, sizeof(key)))
                continue;
        } else if (controllers && b->input.device == 0) {
            if (b->input.kind == QA_PHYSICAL_BUTTON && b->input.code < sizeof(pads) / sizeof(*pads))
                (void)snprintf(key, sizeof(key), "GAMEPAD_%s", pads[b->input.code]);
            else if (b->input.kind == QA_PHYSICAL_AXIS && b->input.positive &&
                     b->input.code >= QA_AXIS_LEFT_TRIGGER)
                (void)snprintf(key, sizeof(key), "GAMEPAD_%s_TRIGGER",
                               b->input.code == QA_AXIS_LEFT_TRIGGER ? "LEFT" : "RIGHT");
            else
                continue;
        } else
            continue;
        if (strpbrk(bound_command, "\"\r\n")) {
            qa_error_set(error, QA_ERROR_FORMAT, 0,
                         "Source cfg cannot encode this binding; use structured seat "
                         "settings");
            success = false;
            break;
        }
        success = qa_input_text_append(&text, "bind \"", 6, error) &&
                  qa_input_text_append(&text, key, strlen(key), error) &&
                  qa_input_text_append(&text, "\" \"", 3, error) &&
                  qa_input_text_append(&text, bound_command, strlen(bound_command), error) &&
                  qa_input_text_append(&text, "\"\n", 2, error);
    }
    if (success)
        *out = (qa_buffer){(uint8_t *)text.scratch, text.scratch_size};
    else
        free(text.scratch);
    return success;
}
bool qa_input_weapon_defaults(qa_input_seat *s, const qa_input_weapon_binding *items, size_t count,
                              qa_error *error) {
    for (size_t i = 0; i < count; ++i) {
        const qa_input_weapon_binding *item = &items[i];
        if (item->powerup || !item->default_key)
            continue;
        qa_physical_input physical;
        if (!qa_input_physical_parse(item->default_key, 0, &physical))
            continue;
        /* The first selected source owning a slot wins within the catalog. */
        bool earlier = false;
        for (size_t j = 0; j < i; ++j)
            if (items[j].default_key && strcmp(items[j].default_key, item->default_key) == 0) {
                earlier = true;
                break;
            }
        if (earlier)
            continue;
        size_t length = strlen(item->id);
        if (length > SIZE_MAX - 5)
            return false;
        char *text = malloc(length + 5);
        if (!text) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating weapon binding");
            return false;
        }
        memcpy(text, "use ", 4);
        memcpy(text + 4, item->id, length + 1);
        qa_input_binding b = {.input = physical, .kind = QA_BIND_COMMAND, .command = text};
        bool success = qa_input_seat_bind(s, &b, error);
        free(text);
        if (!success)
            return false;
    }
    return true;
}
static bool normalized_equal(const char *a, const char *b) {
    for (;;) {
        while (*a == ' ')
            ++a;
        while (*b == ' ')
            ++b;
        unsigned ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z')
            ca += 32;
        if (cb >= 'A' && cb <= 'Z')
            cb += 32;
        if (ca != cb)
            return false;
        if (!ca)
            return true;
        ++a;
        ++b;
    }
}
const qa_input_weapon_binding *qa_input_weapon_resolve(const char *command_name,
                                                       const char *argument,
                                                       const qa_input_weapon_binding *items,
                                                       size_t count) {
    if (!command_name || !argument || !*argument || strpbrk(argument, ";\r\n\\\"") ||
        strstr(argument, "//") || strstr(argument, "/*"))
        return NULL;
    if (qa_input_ascii_equal(command_name, "use")) {
        const qa_input_weapon_binding *match = NULL;
        size_t matches = 0;
        for (size_t i = 0; i < count; ++i) {
            if (normalized_equal(argument, items[i].id))
                return &items[i];
            const char *tail = strrchr(items[i].id, '/');
            if (normalized_equal(argument, items[i].label) ||
                (tail && normalized_equal(argument, tail + 1))) {
                match = &items[i];
                ++matches;
            }
        }
        return matches == 1 ? match : NULL;
    }
    bool impulse = qa_input_ascii_equal(command_name, "impulse");
    if (!impulse && !qa_input_ascii_equal(command_name, "weapon"))
        return NULL;
    if (*argument < '1' || *argument > '9')
        return NULL;
    unsigned number = 0;
    for (const unsigned char *p = (const unsigned char *)argument; *p; ++p) {
        if (*p < '0' || *p > '9' || number > (UINT32_MAX - 9) / 10)
            return NULL;
        number = number * 10 + (unsigned)(*p - '0');
    }
    const qa_input_weapon_binding *match = NULL;
    for (size_t i = 0; i < count; ++i) {
        if (items[i].powerup)
            continue;
        if (impulse && !items[i].q1_impulse)
            return NULL;
        if ((impulse ? items[i].q1_impulse : items[i].q3_weapon) == number) {
            if (match)
                return NULL;
            match = &items[i];
        }
    }
    return match;
}
