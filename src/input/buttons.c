#include "qa/input.h"
#include <stdlib.h>
#include <string.h>

bool qa_input_button_down(qa_input_button *button, uint64_t source, double time, qa_error *error) {
    if (!button || !isfinite(time) || time < 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid button timestamp");
        return false;
    }
    uint64_t *sources = button->sources ? button->sources : button->inline_sources;
    for (size_t i = 0; i < button->count; ++i)
        if (sources[i] == source)
            return true;
    size_t capacity = button->sources ? button->capacity : 4;
    if (button->count == capacity) {
        if (capacity > SIZE_MAX / 2 / sizeof(*sources)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Button source capacity overflow");
            return false;
        }
        uint64_t *grown = realloc(button->sources, capacity * 2 * sizeof(*sources));
        if (!grown) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Allocating button sources");
            return false;
        }
        if (!button->sources)
            memcpy(grown, button->inline_sources, sizeof(button->inline_sources));
        button->sources = sources = grown;
        button->capacity = capacity * 2;
    }
    if (!button->count) {
        button->down_ms = time;
        button->pressed = true;
    }
    sources[button->count++] = source;
    return true;
}

void qa_input_button_up(qa_input_button *button, uint64_t source, double time, double missing) {
    uint64_t *sources = button->sources ? button->sources : button->inline_sources;
    for (size_t i = 0; i < button->count; ++i)
        if (sources[i] == source) {
            sources[i] = sources[--button->count];
            if (!button->count) {
                button->held_ms += time == 0 ? missing : fmax(0, time - button->down_ms);
                button->released = true;
            }
            return;
        }
}

void qa_input_button_release(qa_input_button *button, double time) {
    if (!button->count)
        return;
    button->count = 0;
    button->held_ms += time == 0 ? 10 : fmax(0, time - button->down_ms);
    button->released = true;
}

bool qa_input_button_sample(qa_input_button *button, qa_game_family timing, double now,
                            double frame, float *out, qa_error *error) {
    if (!button || !out || timing < QA_GAME_Q1 || timing > QA_GAME_Q3 || !isfinite(now) ||
        now < 0 || !isfinite(frame) || frame <= 0) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid button sample");
        return false;
    }
    bool active = button->count != 0;
    float value;
    if (timing == QA_GAME_Q1) {
        value = button->pressed && button->released ? (active ? 0.75f : 0.25f)
                : button->pressed                   ? (active ? 0.5f : 0)
                                                    : (active ? 1 : 0);
    } else {
        if (active) {
            button->held_ms += button->down_ms == 0 ? now : fmax(0, now - button->down_ms);
            button->down_ms = now;
        }
        value = timing == QA_GAME_Q3 ? (float)button->held_ms / (float)frame
                                       : (float)(button->held_ms / frame);
        value = fmaxf(0, fminf(1, value));
    }
    button->held_ms = 0;
    button->pressed = button->released = false;
    *out = value;
    return true;
}

void qa_input_button_clear(qa_input_button *button) {
    button->count = 0;
    button->down_ms = button->held_ms = 0;
    button->pressed = button->released = false;
}
void qa_input_button_destroy(qa_input_button *button) {
    if (button) {
        free(button->sources);
        memset(button, 0, sizeof(*button));
    }
}
