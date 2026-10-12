#ifndef QA_CONSOLE_SEAT_INTERNAL_H
#define QA_CONSOLE_SEAT_INTERNAL_H

#include "internal.h"
#include "field_internal.h"
#include "buffer_internal.h"
#include "qa/console_seat.h"
#include "qa/pool.h"

typedef struct completion_inventory {
    qa_arena storage;
    size_t slot;
} completion_inventory;

typedef struct staged_line {
    struct staged_line *next;
    qa_ruleset_id dialect;
    double time;
    char text[];
} staged_line;
struct qa_seat_console {
    qa_seat_console_options options;
    char *script;
    qa_console_buffer *buffer;
    qa_text_field *field, *chat;
    qa_console_history *history;
    qa_arena completion_storage;
    qa_pool completion_leases;
    qa_console *completion_commands;
    const struct cvar_values *completion_values;
    uint64_t completion_command_revision, completion_cvar_revision;
    size_t completion_count;
    bool completion_ready;
    staged_line *staged_first, *staged_last;
    float fraction;
    int32_t chat_target;
    bool staged, opened, suppress_toggle_text, targeted, control, shift;
    size_t active_depth;
};
bool qac_seat_context_ready(const qa_seat_console_options *, const qa_command_context *, qa_error *);

#endif
