#ifndef QA_INPUT_SAVE_H
#define QA_INPUT_SAVE_H
#include "qa/input.h"
#include "qa/input_release.h"
#include "qa/source_save.h"

typedef struct qa_input_checkpoint_refs {
    void *context;
    /* Stable service descriptor qualifies physical ordinal, authored command
     * context and its actual owner qualifier, console/cvars and UI callbacks.
     * Decode returns borrowed candidate services from the restored graph. */
    bool (*services_encode)(void *, const qa_input_seat_options *, uint64_t *, qa_error *);
    bool (*services_decode)(void *, uint64_t, qa_input_seat_options *, qa_error *);
    bool (*ui_encode)(void *, qa_input_ui_handler, void *, uint64_t *, qa_error *);
    bool (*ui_decode)(void *, uint64_t, qa_input_ui_handler *, void **, qa_error *);
    bool (*catcher_ready)(void *, uint64_t, qa_error *);
    /* Qualifies an exact former physical namespace against the enclosing
     * Source graph. Read returns its genuine imported console and registry. */
    bool (*recipient_fields)(void *,qa_source_save_io *,qa_input_seat_options *);
    /* Actual enclosing physical Source custody for a returned retained
     * release. This is separate from ordinary live context admission. */
    bool (*release_ready)(void *,const qa_input_seat_options *,const struct qa_input_release *,qa_error *);
} qa_input_checkpoint_refs;
/* Observe an active installed record, including the base record at token zero.
 * Returned function/context borrows the actual seat; no dispatch or routing
 * changes occur. Inactive and missing saved tokens fail qualification. */
bool qa_input_seat_ui_binding_read(const qa_input_seat *, qa_input_ui_token,
    qa_input_ui_handler *, void **);
/* Returned live owner or genuine retained retirement ALL custody. Does not
 * encode, dispatch, advance or dispose any continuation. */
bool qa_input_seat_checkpoint_ready(const qa_input_seat *,const qa_input_checkpoint_refs *,qa_error *);
/* Call outside input dispatch. Restore keeps the actual seat heap address,
 * reconstructs live/held binding aliases and private command-source IDs,
 * qualifies distinct physical ordinal/authored launch ID and retained former
 * recipient namespaces and retained release programmes (QINS schema 5),
 * and replaces continuation only after full validation. No input, release,
 * UI, command, calibration or device callbacks execute. Inactive UI records'
 * expired handler/user pointers and command formatting scratch are unused. */
bool qa_input_seat_checkpoint(const qa_input_seat *, const qa_input_checkpoint_refs *, qa_buffer *, qa_error *);
bool qa_input_seat_restore(qa_input_seat *, qa_bytes, const qa_input_checkpoint_refs *, qa_error *);
#endif
