#ifndef QA_INPUT_SAVE_H
#define QA_INPUT_SAVE_H
#include "qa/input.h"

typedef struct qa_input_checkpoint_refs {
    void *context;
    /* Stable service descriptor qualifies console/cvars, command context and
     * base/source UI callbacks. Decode returns borrowed candidate services. */
    bool (*services_encode)(void *, const qa_input_seat_options *, uint64_t *, qa_error *);
    bool (*services_decode)(void *, uint64_t, qa_input_seat_options *, qa_error *);
    bool (*ui_encode)(void *, qa_input_ui_handler, void *, uint64_t *, qa_error *);
    bool (*ui_decode)(void *, uint64_t, qa_input_ui_handler *, void **, qa_error *);
    bool (*catcher_ready)(void *, uint64_t, qa_error *);
} qa_input_checkpoint_refs;
/* Observe an active installed record, including the base record at token zero.
 * Returned function/context borrows the actual seat; no dispatch or routing
 * changes occur. Inactive and missing saved tokens fail qualification. */
bool qa_input_seat_ui_binding_read(const qa_input_seat *, qa_input_ui_token,
    qa_input_ui_handler *, void **);
/* Call outside input dispatch. Restore keeps the actual seat heap address,
 * reconstructs live/held binding aliases and private command-source IDs,
 * and replaces continuation only after full validation. No input, release,
 * UI, command, calibration or device callbacks execute. Inactive UI records'
 * expired handler/user pointers and command formatting scratch are unused. */
bool qa_input_seat_checkpoint(const qa_input_seat *, const qa_input_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_input_seat_restore(qa_input_seat *, qa_bytes, const qa_input_checkpoint_refs *, qa_error *);
#endif
