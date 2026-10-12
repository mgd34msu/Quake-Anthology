#include "qa/input.h"
#include <string.h>

bool qa_input_button_prepare(qa_input_button *button,size_t capacity,qa_error *error) {
    if(!button) {qa_error_set(error,QA_ERROR_ARGUMENT,0,"Button preparation requires its owner");return false;}
    size_t current=button->sources?button->capacity:4;
    if(capacity<=current) return true;
    if(capacity>SIZE_MAX/sizeof(*button->sources)) {
        qa_error_set(error,QA_ERROR_MEMORY,0,"Button source capacity overflow");
        return false;
    }
    bool sealed=button->source_storage.sealed;
    button->source_storage.sealed=false;
    uint64_t *sources=button->sources?button->sources:button->inline_sources;
    uint64_t *grown=qa_arena_grow(&button->source_storage,sources,
        button->count*sizeof(*sources),capacity*sizeof(*sources),_Alignof(uint64_t),error);
    button->source_storage.sealed=sealed;
    if(!grown) return false;
    button->sources=grown;button->capacity=capacity;
    return true;
}
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
        if(button->source_storage.sealed) {
            qa_error_set(error,QA_ERROR_MEMORY,0,"Button source exceeds its admitted input storage");
            return false;
        }
        if (capacity > SIZE_MAX / 2 / sizeof(*sources)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Button source capacity overflow");
            return false;
        }
        if(!qa_input_button_prepare(button,capacity*2,error)) return false;
        sources=button->sources;
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
        qa_arena_destroy(&button->source_storage);
        memset(button, 0, sizeof(*button));
    }
}
