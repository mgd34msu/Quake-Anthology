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
    char **matches, *tail, *completed;
    size_t match_count, match_index, completed_capacity;
};
struct qa_console_history {
    char **entries, *draft;
    size_t capacity, count, first, position;
};

#endif
