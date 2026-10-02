#ifndef QA_CONSOLE_SEAT_H
#define QA_CONSOLE_SEAT_H
#include "qa/console_buffer.h"
#include "qa/console_discovery.h"
#include "qa/field.h"
#include "qa/input.h"

typedef struct qa_seat_console qa_seat_console;
typedef struct qa_seat_console_options {
    uint32_t seat; /* Physical route; command.seat is the authored launch ID. */
    qa_command_context command;
    qa_console *commands;
    bool staged;
    void *context;
    bool (*context_ready)(void *, uint32_t physical_seat, const qa_command_context *, qa_error *);
    double (*now_ms)(void *);
    bool (*connected)(void *);
    bool (*clipboard)(void *, qa_buffer *, qa_error *);
    bool (*focus)(void *, qa_input_focus, bool team, qa_error *);
    bool (*chat)(void *, const char *text, bool team, bool targeted, int32_t target, qa_error *);
} qa_seat_console_options;
/* Owner callbacks may route input and queue commands. They must not destroy
 * or reconfigure this console until the active call returns. */
qa_seat_console *qa_seat_console_create(const qa_seat_console_options *, qa_error *);
void qa_seat_console_destroy(qa_seat_console *);
qa_command_context qa_seat_console_context_read(const qa_seat_console *);
/* Qualify an uncaptured ENGINE template at the idle owner boundary, then
 * publish that same template without callbacks. The caller keeps the actual
 * candidate/current publication and both input/console owners unchanged. */
bool qa_seat_console_context_ready(const qa_seat_console *, const qa_command_context *, qa_error *);
void qa_seat_console_context_publish(qa_seat_console *, const qa_command_context *);
qa_console *qa_seat_console_recipient_read(const qa_seat_console *);
bool qa_seat_console_recipient_ready_is(const qa_seat_console *,const qa_console *,const qa_command_context *);
bool qa_seat_console_recipient_ready(const qa_seat_console *,qa_console *,const qa_command_context *,qa_error *);
void qa_seat_console_recipient_publish(qa_seat_console *,qa_console *,const qa_command_context *);
qa_console_buffer *qa_seat_console_buffer(qa_seat_console *);
qa_text_field *qa_seat_console_field(qa_seat_console *, bool chat);
qa_console_history *qa_seat_console_history(qa_seat_console *);
bool qa_seat_console_print(qa_seat_console *, const char *, qa_error *);
void qa_seat_console_publish(qa_seat_console *, qa_input_focus);
/* Consumes the candidate publication once; retained output/history/editing
 * state stays with the existing seat. Both objects remain caller-owned. */
bool qa_seat_console_adopt(qa_seat_console *, qa_seat_console *candidate, qa_input_focus,
                           qa_error *);
bool qa_seat_console_open(qa_seat_console *, bool open, qa_error *);
bool qa_seat_console_toggle(qa_seat_console *, bool from_key, bool repeat, qa_error *);
bool qa_seat_console_message(qa_seat_console *, bool team, bool targeted, int32_t target,
                             qa_error *);
bool qa_seat_console_submit(qa_seat_console *, qa_error *);
bool qa_seat_console_input(qa_seat_console *, const qa_input_event *, qa_input_focus, bool team,
                           bool *handled, qa_error *);
float qa_seat_console_animate(qa_seat_console *, bool open, float elapsed_ms, float speed,
                              float target_fraction);
/* Matching entry borrows registry strings and remains valid until mutation. */
bool qa_seat_console_selected(qa_seat_console *, qa_console_discovery_entry *out);
#endif
