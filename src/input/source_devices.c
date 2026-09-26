#include "qa/input.h"
#include <string.h>
static const int joystick_keys[] = {
    QA_KEY_LEFT,      QA_KEY_RIGHT,     QA_KEY_UP,        QA_KEY_DOWN,
    QA_KEY_JOY1 + 15, QA_KEY_JOY1 + 16, QA_KEY_JOY1 + 17, QA_KEY_JOY1 + 18,
    QA_KEY_JOY1 + 19, QA_KEY_JOY1 + 20, QA_KEY_JOY1 + 21, QA_KEY_JOY1 + 22,
    QA_KEY_JOY1 + 23, QA_KEY_JOY1 + 24, QA_KEY_JOY1 + 25, QA_KEY_JOY1 + 26};
bool qa_source_joystick_button(qa_source_joystick *s, unsigned button, bool down, bool transitions,
                               qa_input_key_sink key, void *user, qa_error *error) {
    if (button >= 256 || !s || !key) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid source joystick button");
        return false;
    }
    if (transitions && s->buttons[button] == down)
        return true;
    if (!key(user, QA_KEY_JOY1 + (int)button, down, 0, error))
        return false;
    s->buttons[button] = down;
    return true;
}
bool qa_source_joystick_frame(qa_source_joystick *s, bool connected, bool windows, float threshold,
                              unsigned count, float scale, qa_input_key_sink key,
                              qa_input_mouse_sink mouse, void *user, qa_error *error) {
    if (!s || !key || !isfinite(threshold) || !isfinite(scale) || (windows && !mouse)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid source joystick frame");
        return false;
    }
    uint32_t axes = 0;
    unsigned limit = windows ? (count < 4 ? count : 4) : 16;
    if (connected) {
        for (unsigned i = 0; i < limit; ++i) {
            float f = (float)s->axes[i] / (windows ? 32768.0f : 32767.0f);
            if (f < -threshold)
                axes |= UINT32_C(1) << (i * 2);
            else if (f > threshold)
                axes |= UINT32_C(1) << (i * 2 + 1);
        }
        if (windows) {
            if (s->hat == 1)
                axes |= 1u << 12;
            else if (s->hat == 4)
                axes |= 1u << 13;
            else if (s->hat == 2)
                axes |= 1u << 14;
            else if (s->hat == 8)
                axes |= 1u << 15;
        }
    }
    for (unsigned i = 0; i < 16; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if ((axes & bit) != (s->old_axes & bit)) {
            if (!key(user, joystick_keys[i], (axes & bit) != 0, 0, error))
                return false;
            s->old_axes = (s->old_axes & ~bit) | (axes & bit);
        }
    }
    if (connected && windows && count >= 6) {
        float x = truncf((float)s->axes[4] * scale), y = truncf((float)s->axes[5] * scale);
        if ((x != 0 || y != 0) && !mouse(user, x, y, 0, error))
            return false;
    }
    return true;
}
bool qa_source_joystick_release(qa_source_joystick *s, double time, qa_input_key_sink key,
                                void *user, qa_error *error) {
    bool success = true;
    bool released[QA_KEY_JOY1 + 256] = {0};
    for (unsigned i = 0; i < 256; ++i)
        if (s->buttons[i]) {
            int k = QA_KEY_JOY1 + (int)i;
            if (!key(user, k, false, time, error))
                success = false;
            released[k] = true;
        }
    for (unsigned i = 0; i < 16; ++i)
        if ((s->old_axes & (1u << i)) && !released[joystick_keys[i]])
            if (!key(user, joystick_keys[i], false, time, error))
                success = false;
    memset(s, 0, sizeof(*s));
    return success;
}
bool qa_midi_feed(qa_midi_decoder *s, qa_bytes bytes, int channel, double time,
                  qa_input_key_sink key, void *user, qa_error *error) {
    if (!s || (!bytes.data && bytes.size) || !key || !isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid MIDI input");
        return false;
    }
    for (size_t i = 0; i < bytes.size; ++i) {
        uint8_t byte = bytes.data[i];
        if (byte >= 0xf8)
            continue;
        if (byte >= 0x80) {
            s->status = byte < 0xf0 ? byte : 0;
            s->has_first = false;
            continue;
        }
        if (!s->status)
            continue;
        unsigned command = s->status & 0xf0;
        if (command == 0xc0 || command == 0xd0)
            continue;
        if (!s->has_first) {
            s->first = byte;
            s->has_first = true;
            continue;
        }
        unsigned note = s->first;
        s->has_first = false;
        if ((int)(s->status & 15) + 1 != channel || (command != 0x80 && command != 0x90))
            continue;
        int code = (int)note - 60 + QA_KEY_AUX1;
        if (code < QA_KEY_AUX1 || code > 255)
            continue;
        if ((command == 0x80 || byte == 0) && !key(user, code, false, time, error))
            return false;
        /* Original velocity-zero note-on emits release and then press. */
        if (command == 0x90 && !key(user, code, true, time, error))
            return false;
    }
    return true;
}
