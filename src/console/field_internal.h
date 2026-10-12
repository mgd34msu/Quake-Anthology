#ifndef QA_CONSOLE_FIELD_INTERNAL_H
#define QA_CONSOLE_FIELD_INTERNAL_H

#include "qa/field.h"
#include "qa/arena.h"

struct qa_text_field {
    uint32_t *characters;
    char *text;
    size_t maximum, length, cursor, scroll, width;
    bool overstrike, dirty, selected;
    qa_arena completion_storage;
    qa_field_completion_names ordered_names;
    char **matches, *tail, *completed;
    size_t match_count, match_capacity, match_index, completed_capacity;
};
bool qac_field_completion_prepare(qa_text_field *, size_t count, size_t text_bytes,
    size_t maximum_name, qa_error *);
struct qa_console_history {
    char **entries, *draft;
    size_t capacity, count, first, position;
};

#endif
