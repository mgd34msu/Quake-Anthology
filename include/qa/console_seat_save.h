#ifndef QA_CONSOLE_SEAT_SAVE_H
#define QA_CONSOLE_SEAT_SAVE_H

#include "qa/console_seat.h"

typedef struct qa_seat_console_save_resolvers {
    void *context;
    /* Encode genuine session/provider/client/actor/publication identities.
     * The returned buffer is owned. Editing state and private script text are
     * encoded by the seat owner, never interned into session strings. */
    bool (*command_encode)(void *, const qa_seat_console_options *, qa_buffer *, qa_error *);
    /* Qualify the installed candidate's actual commands, callbacks and heap.
     * Only identity stamps may change. Keep the parsed dialect, origin, seat,
     * direct/console-text flags and borrowed private script pointer unchanged.
     * Neither resolver dispatches commands or changes either owner. */
    bool (*command_decode)(void *, const qa_seat_console_options *, qa_bytes,
                            qa_command_context *, qa_error *);
} qa_seat_console_save_resolvers;

bool qa_seat_console_idle(const qa_seat_console *);
/* Capture requires an empty output buffer. */
bool qa_seat_console_save_capture(qa_seat_console *, const qa_seat_console_save_resolvers *,
                                  qa_buffer *, qa_error *);
/* All allocations remain scratch-owned until a complete validated exchange.
 * The installed seat, fields, history, buffer, callback context and command
 * owner keep their identity. Only their validated private contents change.
 * Publication, focus, clock, chat, clipboard and source callbacks never run. */
bool qa_seat_console_save_restore(qa_seat_console *, const qa_seat_console_save_resolvers *,
                                  qa_bytes, qa_error *);

#endif
