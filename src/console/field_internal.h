#ifndef QA_CONSOLE_FIELD_INTERNAL_H
#define QA_CONSOLE_FIELD_INTERNAL_H

#include "qa/field.h"

struct qa_text_field {
    uint32_t *characters;
    char *text;
    size_t maximum, length, cursor, scroll, width;
    bool overstrike, dirty, selected;
    char **matches, *tail;
    size_t match_count, match_index;
};
struct qa_console_history {
    char **entries, *draft;
    size_t capacity, count, first, position;
};

#endif
