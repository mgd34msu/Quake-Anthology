#include "internal.h"
#include "qa/text.h"
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct key_name {
    const char *name;
    int code;
};
static const struct key_name names[] = {{"TAB", QA_KEY_TAB},
                                        {"ENTER", QA_KEY_ENTER},
                                        {"ESCAPE", QA_KEY_ESCAPE},
                                        {"SPACE", QA_KEY_SPACE},
                                        {"BACKSPACE", QA_KEY_BACKSPACE},
                                        {"UPARROW", QA_KEY_UP},
                                        {"DOWNARROW", QA_KEY_DOWN},
                                        {"LEFTARROW", QA_KEY_LEFT},
                                        {"RIGHTARROW", QA_KEY_RIGHT},
                                        {"ALT", QA_KEY_ALT},
                                        {"CTRL", QA_KEY_CONTROL},
                                        {"SHIFT", QA_KEY_SHIFT},
                                        {"COMMAND", QA_KEY_COMMAND},
                                        {"CAPSLOCK", QA_KEY_CAPSLOCK},
                                        {"INS", QA_KEY_INSERT},
                                        {"DEL", QA_KEY_DELETE},
                                        {"PGDN", QA_KEY_PAGEDOWN},
                                        {"PGUP", QA_KEY_PAGEUP},
                                        {"HOME", QA_KEY_HOME},
                                        {"END", QA_KEY_END},
                                        {"MWHEELUP", QA_KEY_WHEEL_UP},
                                        {"MWHEELDOWN", QA_KEY_WHEEL_DOWN},
                                        {"KP_HOME", QA_KEY_KP_HOME},
                                        {"KP_UPARROW", QA_KEY_KP_UP},
                                        {"KP_PGUP", QA_KEY_KP_PAGEUP},
                                        {"KP_LEFTARROW", QA_KEY_KP_LEFT},
                                        {"KP_5", QA_KEY_KP_5},
                                        {"KP_RIGHTARROW", QA_KEY_KP_RIGHT},
                                        {"KP_END", QA_KEY_KP_END},
                                        {"KP_DOWNARROW", QA_KEY_KP_DOWN},
                                        {"KP_PGDN", QA_KEY_KP_PAGEDOWN},
                                        {"KP_ENTER", QA_KEY_KP_ENTER},
                                        {"KP_INS", QA_KEY_KP_INSERT},
                                        {"KP_DEL", QA_KEY_KP_DELETE},
                                        {"KP_SLASH", QA_KEY_KP_SLASH},
                                        {"KP_MINUS", QA_KEY_KP_MINUS},
                                        {"KP_PLUS", QA_KEY_KP_PLUS},
                                        {"KP_NUMLOCK", QA_KEY_KP_NUMLOCK},
                                        {"KP_STAR", QA_KEY_KP_STAR},
                                        {"KP_EQUALS", QA_KEY_KP_EQUALS},
                                        {"PAUSE", QA_KEY_PAUSE},
                                        {"SEMICOLON", 59}};
static unsigned fold(unsigned c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; }
bool qa_input_ascii_equal(const char *a, const char *b) {
    while (*a && *b)
        if (fold((unsigned char)*a++) != fold((unsigned char)*b++))
            return false;
    return *a == *b;
}
static unsigned nibble(unsigned c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : 0;
}
const char *qa_input_key_name(int key, char out[32]) {
    if (key == -1)
        return "<KEY NOT FOUND>";
    if (key < 0 || key > 271)
        return "<OUT OF RANGE>";
    if (key > 32 && key < 127 && key != '"' && key != ';') {
        out[0] = (char)key;
        out[1] = 0;
        return out;
    }
    for (size_t i = 0; i < sizeof(names) / sizeof(*names); ++i)
        if (key == names[i].code)
            return names[i].name;
    if (key >= QA_KEY_F1 && key <= QA_KEY_F12)
        (void)snprintf(out, 32, "F%d", key - QA_KEY_F1 + 1);
    else if (key >= QA_KEY_MOUSE1 && key <= QA_KEY_MOUSE5)
        (void)snprintf(out, 32, "MOUSE%d", key - QA_KEY_MOUSE1 + 1);
    else if (key >= QA_KEY_JOY1 && key <= QA_KEY_JOY32)
        (void)snprintf(out, 32, "JOY%d", key - QA_KEY_JOY1 + 1);
    else if (key >= QA_KEY_AUX1 && key <= QA_KEY_AUX16)
        (void)snprintf(out, 32, "AUX%d", key - QA_KEY_AUX1 + 1);
    else if (key >= QA_KEY_AUX17 && key <= QA_KEY_AUX32)
        (void)snprintf(out, 32, "AUX%d", key - QA_KEY_AUX17 + 17);
    else
        (void)snprintf(out, 32, "0x%02x", (unsigned)key);
    return out;
}
int qa_input_key_parse(const char *text) {
    if (!text || !*text)
        return -1;
    if (!text[1])
        return (unsigned char)*text < 128 ? (int)(unsigned char)*text
                                          : (int)(unsigned char)*text - 256;
    if (strlen(text) == 4 && text[0] == '0' && text[1] == 'x')
        return (int)(16 * nibble((unsigned char)text[2]) + nibble((unsigned char)text[3]));
    char buffer[32];
    for (int i = 0; i <= 271; ++i)
        if (qa_input_ascii_equal(text, qa_input_key_name(i, buffer)))
            return i;
    return -1;
}
unsigned qa_input_mouse_button(unsigned button) {
    return button == 2 ? 3 : button == 3 ? 2 : button;
}
int qa_input_source_key(int key, qa_ruleset_id dialect) {
    if (key < 0)
        return -1;
    if (dialect == QA_RULESET_Q3)
        return key <= 255 ? key : -1;
    if (key < 128)
        return key;
    if (key >= QA_KEY_UP && key <= QA_KEY_SHIFT)
        return key - 4;
    if (key >= QA_KEY_INSERT && key <= QA_KEY_END)
        return key + 8;
    if (key >= QA_KEY_F1 && key <= QA_KEY_F12)
        return key - 10;
    if (key == QA_KEY_PAUSE)
        return 255;
    if (key >= QA_KEY_MOUSE1 && key <= QA_KEY_MOUSE1 + 2)
        return key - QA_KEY_MOUSE1 + 200;
    if (key >= QA_KEY_JOY1 && key <= QA_KEY_JOY1 + 3)
        return key - QA_KEY_JOY1 + 203;
    if (key >= QA_KEY_AUX1 && key <= QA_KEY_AUX16)
        return key - QA_KEY_AUX1 + 207;
    if (key >= QA_KEY_AUX17 && key <= QA_KEY_AUX32)
        return key - QA_KEY_AUX17 + 223;
    bool q2 = dialect == QA_RULESET_Q2_CLASSIC || dialect == QA_RULESET_Q2_RERELEASE;
    if (key == QA_KEY_WHEEL_UP)
        return q2 ? 240 : 239;
    if (key == QA_KEY_WHEEL_DOWN)
        return q2 ? 239 : 240;
    if (q2 && key >= QA_KEY_KP_HOME && key <= QA_KEY_KP_PLUS)
        return key;
    return -1;
}
uint32_t qa_input_q3_catcher_from_source(uint32_t mask) {
    return (mask & ~UINT32_C(12)) | ((mask & 4u) ? QA_INPUT_CATCH_CHAT : 0u) |
           ((mask & 8u) ? QA_INPUT_CATCH_GAME : 0u);
}
uint32_t qa_input_q3_catcher_to_source(uint32_t mask) {
    return (mask & ~(uint32_t)(QA_INPUT_CATCH_CHAT | QA_INPUT_CATCH_GAME)) |
           ((mask & QA_INPUT_CATCH_CHAT) ? 4u : 0u) |
           ((mask & QA_INPUT_CATCH_GAME) ? 8u : 0u);
}
int32_t qa_input_client_key(const qa_input_event *input) {
    if (!input || (input->kind != QA_INPUT_EVENT_KEY && input->kind != QA_INPUT_EVENT_BUTTON))
        return -1;
    uint32_t code = input->input.code;
    if (input->input.kind == QA_PHYSICAL_MOUSE) {
        if (code < 1 || code > 255) return -1;
        unsigned button = qa_input_mouse_button(code);
        if (button < 1 || button > 5) return -1;
        code = QA_KEY_MOUSE1 + button - 1;
    } else if (input->input.kind != QA_PHYSICAL_KEY) return -1;
    return code <= INT_MAX ? qa_input_source_key((int)code, QA_RULESET_Q3) : -1;
}
static bool client_key(const qa_input_client_sink *sink, int32_t code, bool down, qa_error *error) {
    if (!sink->key(sink->context, code, down, error)) return false;
    if (sink->held_keys && code >= 0 && code < 256) (*sink->held_keys)[code] = down;
    return true;
}
bool qa_input_client_event(const qa_input_client_sink *sink, const qa_input_event *input,
                           bool *handled, qa_error *error) {
    if (!sink || !sink->seat || !sink->key || !input || !handled) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Client input requires its seat and event services");
        return false;
    }
    *handled = false;
    int32_t code = qa_input_client_key(input);
    bool release = code >= 0 && !input->down;
    bool held = release && sink->held_keys && code < 256 && (*sink->held_keys)[code];
    qa_input_focus focus = qa_input_seat_focus(sink->seat);
    uint32_t composed = qa_input_seat_catcher(sink->seat, 0);
    uint32_t catcher = qa_input_seat_catcher(sink->seat, sink->owner);
    if (!held && (focus == QA_INPUT_CONSOLE || focus == QA_INPUT_CHAT || focus == QA_INPUT_UI ||
        (composed & (QA_INPUT_CATCH_CONSOLE | QA_INPUT_CATCH_CHAT)) ||
        (sink->catcher == QA_INPUT_CATCH_GAME && (composed & QA_INPUT_CATCH_UI)) ||
        !(catcher & sink->catcher))) return true;
    if (code >= 0) {
        *handled = true;
        if (code == QA_KEY_ESCAPE && input->down && sink->catcher == QA_INPUT_CATCH_GAME && sink->handling)
            return qa_input_seat_set_catcher(sink->seat, sink->owner,
                catcher & ~(uint32_t)QA_INPUT_CATCH_GAME, error) && sink->handling(sink->context, 0, error);
        return client_key(sink, code, input->down, error);
    }
    if (input->kind == QA_INPUT_EVENT_TEXT && input->text) {
        qa_bytes text = {(const uint8_t *)input->text, strlen(input->text)};
        if (!qa_utf8_valid(text)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Client text input is not UTF-8");
            return false;
        }
        size_t cursor = 0;
        uint32_t scalar;
        while (qa_utf8_next(text, &cursor, &scalar))
            if (!client_key(sink, (int32_t)scalar | QA_KEY_CHAR_FLAG, true, error)) return false;
        *handled = true;
    } else if (input->kind == QA_INPUT_EVENT_MOUSE && sink->mouse) {
        if (!isfinite(input->delta.x) || !isfinite(input->delta.y) ||
            (double)input->delta.x < INT32_MIN || (double)input->delta.x > INT32_MAX ||
            (double)input->delta.y < INT32_MIN || (double)input->delta.y > INT32_MAX) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Client mouse delta exceeds source integer range");
            return false;
        }
        *handled = true;
        return sink->mouse(sink->context, (int32_t)input->delta.x, (int32_t)input->delta.y, error);
    } else if (input->kind == QA_INPUT_EVENT_WHEEL) {
        if (!isfinite(input->delta.y) || fabsf(input->delta.y) > UINT16_MAX) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Client wheel delta exceeds source range");
            return false;
        }
        if (input->delta.y == 0.0f) return true;
        code = input->delta.y > 0 ? QA_KEY_WHEEL_UP : QA_KEY_WHEEL_DOWN;
        *handled = true;
        unsigned count = (unsigned)ceilf(fabsf(input->delta.y));
        for (unsigned i = 0; i < count; ++i)
            if (!client_key(sink, code, true, error) || !client_key(sink, code, false, error)) return false;
    }
    return true;
}
int qa_input_sdl_key(int32_t code, uint16_t mod) {
    if ((code & INT32_C(0x40000000)) != 0) {
        int key = code & ~INT32_C(0x40000000);
        if (key >= 58 && key <= 69)
            return QA_KEY_F1 + key - 58;
        if (key == 93 && (mod & 0x1000))
            return (mod & 0xc0) ? 29 : '5';
        switch (key) {
        case 72:
            return QA_KEY_PAUSE;
        case 73:
            return QA_KEY_INSERT;
        case 74:
            return QA_KEY_HOME;
        case 75:
            return QA_KEY_PAGEUP;
        case 77:
            return QA_KEY_END;
        case 78:
            return QA_KEY_PAGEDOWN;
        case 79:
            return QA_KEY_RIGHT;
        case 80:
            return QA_KEY_LEFT;
        case 81:
            return QA_KEY_DOWN;
        case 82:
            return QA_KEY_UP;
        case 84:
            return QA_KEY_KP_SLASH;
        case 85:
            return '*';
        case 86:
            return QA_KEY_KP_MINUS;
        case 87:
            return QA_KEY_KP_PLUS;
        case 88:
            return QA_KEY_KP_ENTER;
        case 89:
            return QA_KEY_KP_END;
        case 90:
            return QA_KEY_KP_DOWN;
        case 91:
            return QA_KEY_KP_PAGEDOWN;
        case 92:
            return QA_KEY_KP_LEFT;
        case 93:
            return QA_KEY_KP_5;
        case 94:
            return QA_KEY_KP_RIGHT;
        case 95:
            return QA_KEY_KP_HOME;
        case 96:
            return QA_KEY_KP_UP;
        case 97:
            return QA_KEY_KP_PAGEUP;
        case 98:
            return QA_KEY_KP_INSERT;
        case 99:
            return QA_KEY_KP_DELETE;
        case 103:
            return '=';
        case 116:
        case 224:
        case 228:
            return QA_KEY_CONTROL;
        case 225:
        case 229:
            return QA_KEY_SHIFT;
        case 226:
        case 227:
        case 230:
        case 231:
            return QA_KEY_ALT;
        case 205:
            return QA_KEY_SPACE;
        default:
            return 0;
        }
    }
    switch (code) {
    case 8:
        return QA_KEY_BACKSPACE;
    case 127:
        return QA_KEY_DELETE;
    case '!':
        return '1';
    case '@':
        return '2';
    case '#':
        return '3';
    case '$':
        return '4';
    case '%':
        return '5';
    case '^':
        return '6';
    case '&':
        return '7';
    case '*':
        return '8';
    case '(':
        return '9';
    case ')':
        return '0';
    case 178:
        return '~';
    case 9:
        return (mod & 3) ? 0 : QA_KEY_TAB;
    case 13:
        return QA_KEY_ENTER;
    case 27:
        return QA_KEY_ESCAPE;
    case 32:
        return QA_KEY_SPACE;
    default:
        break;
    }
    if (mod & 0xc0) {
        if ((code >= 64 && code < 127) || code == 32)
            code &= 31;
        else if (code == '2')
            code = 0;
        else if (code >= '3' && code <= '7')
            code -= 24;
        else if (code == '8')
            code = 127;
        else if (code == '/')
            code = 31;
    }
    if (code >= 'A' && code <= 'Z')
        return code + 32;
    if (code >= 1 && code <= 26)
        return code + 96;
    return code > 0 && code <= 255 ? code : 0;
}
double qa_input_event_time(uint32_t timestamp, uint32_t ticks, double now, bool subframe) {
    uint32_t age = ticks - timestamp;
    return subframe && age <= 30 ? fmax(0, now - (double)age) : now;
}
static const struct { const char *token, *label; } pad_names[] = {
    {"a_button", "A"}, {"b_button", "B"}, {"x_button", "X"}, {"y_button", "Y"},
    {"back", "Back"}, {"guide", "Guide"}, {"start", "Start"},
    {"left_stick", "Left stick press"}, {"right_stick", "Right stick press"},
    {"left_shoulder", "Left shoulder"}, {"right_shoulder", "Right shoulder"},
    {"dpad_up", "D-pad up"}, {"dpad_down", "D-pad down"},
    {"dpad_left", "D-pad left"}, {"dpad_right", "D-pad right"},
    {"misc", "Miscellaneous"}, {"paddle1", "Paddle 1"}, {"paddle2", "Paddle 2"},
    {"paddle3", "Paddle 3"}, {"paddle4", "Paddle 4"}, {"touchpad", "Touchpad"}};
bool qa_input_physical_parse(const char *name, int32_t device, qa_physical_input *out) {
    if (!name || !out || device < 0)
        return false;
    char lower[96];
    size_t n = strlen(name);
    if (n >= sizeof(lower))
        return false;
    for (size_t i = 0; i <= n; ++i)
        lower[i] = (char)fold((unsigned char)name[i]);
    if (strncmp(lower, "mouse", 5) == 0 && lower[5] >= '1' && lower[5] <= '9') {
        char *end;
        unsigned long v = strtoul(lower + 5, &end, 10);
        if (*end || v > 255)
            return false;
        *out = (qa_physical_input){.kind = QA_PHYSICAL_MOUSE,
                                   .code = qa_input_mouse_button((unsigned)v)};
        return true;
    }
    const char *pad = strncmp(lower, "gamepad_", 8) == 0 ? lower + 8 : lower;
    for (size_t i = 0; i < sizeof(pad_names) / sizeof(*pad_names); ++i)
        if (strcmp(pad, pad_names[i].token) == 0) {
            *out = (qa_physical_input){
                .kind = QA_PHYSICAL_BUTTON, .device = device, .code = (uint32_t)i};
            return true;
        }
    if (strcmp(pad, "left_trigger") == 0 || strcmp(pad, "right_trigger") == 0) {
        *out = (qa_physical_input){.kind = QA_PHYSICAL_AXIS,
                                   .device = device,
                                   .code = strcmp(pad, "left_trigger") == 0 ? QA_AXIS_LEFT_TRIGGER
                                                                            : QA_AXIS_RIGHT_TRIGGER,
                                   .positive = true};
        return true;
    }
    int key = qa_input_key_parse(lower);
    if (key < 0)
        return false;
    *out = (qa_physical_input){.kind = QA_PHYSICAL_KEY, .code = (uint32_t)key};
    return true;
}
bool qa_input_physical_name(qa_physical_input input, char *out, size_t size) {
    if (!out || !size)
        return false;
    int n;
    char temp[32];
    switch (input.kind) {
    case QA_PHYSICAL_KEY:
        n = snprintf(out, size, "%s", qa_input_key_name((int)input.code, temp));
        break;
    case QA_PHYSICAL_MOUSE:
        n = snprintf(out, size, "MOUSE%u", qa_input_mouse_button(input.code));
        break;
    case QA_PHYSICAL_BUTTON:
        if (input.code < sizeof(pad_names) / sizeof(*pad_names))
            n = snprintf(out, size, "Pad %lld %s", (long long)input.device + 1,
                         pad_names[input.code].label);
        else
            n = snprintf(out, size, "Pad %lld Button %u", (long long)input.device + 1,
                         input.code + 1);
        break;
    case QA_PHYSICAL_AXIS: {
        static const char *const axes[][2] = {{"Left stick left", "Left stick right"},
            {"Left stick up", "Left stick down"}, {"Right stick left", "Right stick right"},
            {"Right stick up", "Right stick down"}, {"Left trigger released", "Left trigger"},
            {"Right trigger released", "Right trigger"}};
        if (input.code >= QA_AXIS_COUNT)
            return false;
        n = snprintf(out, size, "Pad %lld %s", (long long)input.device + 1,
                     axes[input.code][input.positive ? 1 : 0]);
        break;
    }
    default:
        return false;
    }
    return n >= 0 && (size_t)n < size;
}
