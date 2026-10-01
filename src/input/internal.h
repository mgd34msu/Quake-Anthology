#ifndef QA_INPUT_INTERNAL_H
#define QA_INPUT_INTERNAL_H
#include "qa/input.h"
#include "qa/strings.h"
#include <stdlib.h>
#include <string.h>

typedef struct qa_binding_record {
    qa_input_binding view;
    size_t references;
    char text[];
} qa_binding_record;
typedef struct qa_held_binding {
    qa_physical_input input;
    qa_binding_record *binding;
} qa_held_binding;
typedef struct qa_ui_record {
    qa_input_ui_token id;
    qa_input_ui_handler handler;
    void *user;
    qa_input_focus focus;
    bool active;
} qa_ui_record;
struct qa_input_seat {
    struct qa_input_release *release;
    qa_input_seat_options options;
    qa_input_button buttons[QA_INPUT_ACTION_COUNT];
    qa_gamepad_input gamepad;
    qa_input_pair mouse;
    qa_input_focus focus;
    bool focused;
    qa_input_catcher *catchers;
    size_t catcher_count, catcher_capacity;
    uint8_t impulse;
    qa_binding_record **bindings;
    size_t binding_count, binding_capacity;
    qa_held_binding *held;
    size_t held_count, held_capacity;
    qa_ui_record *ui;
    size_t ui_count, ui_capacity;
    qa_input_ui_token next_ui;
    qa_strings *command_sources;
    char *scratch;
    size_t scratch_size, scratch_capacity;
};
bool qa_input_release_action_access(const qa_input_seat *, qa_error *);
bool qa_input_release_mutation_access(const qa_input_seat *, qa_error *);
bool qa_input_binding_release_text(qa_input_seat *, const qa_held_binding *, double,
    const char **, qa_error *);
bool qa_input_reserve(void **, size_t *capacity, size_t needed, size_t stride, qa_error *);
bool qa_input_text_append(qa_input_seat *, const char *, size_t, qa_error *);
bool qa_input_physical_equal(qa_physical_input, qa_physical_input);
uint64_t qa_input_physical_source(qa_physical_input);
bool qa_input_command_source(qa_input_seat *, const char *, uint64_t *, qa_error *);
void qa_input_binding_record_release(qa_binding_record *);
bool qa_input_ascii_equal(const char *, const char *);
#endif
