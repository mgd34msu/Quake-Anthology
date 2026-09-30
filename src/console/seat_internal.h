#ifndef QA_CONSOLE_SEAT_INTERNAL_H
#define QA_CONSOLE_SEAT_INTERNAL_H

#include "internal.h"
#include "field_internal.h"
#include "buffer_internal.h"
#include "qa/console_seat.h"

typedef struct staged_line {
    struct staged_line *next;
    qa_console_dialect dialect;
    double time;
    char text[];
} staged_line;
struct qa_seat_console {
    qa_seat_console_options options;
    char *script;
    qa_console_buffer *buffer;
    qa_text_field *field, *chat;
    qa_console_history *history;
    staged_line *staged_first, *staged_last;
    float fraction;
    int32_t chat_target;
    bool staged, opened, suppress_toggle_text, targeted, control, shift;
    size_t active_depth;
};

#endif
