#ifndef QA_NATIVE_REGION_SCOPE_H
#define QA_NATIVE_REGION_SCOPE_H
#include "qa/native.h"

typedef struct qa_native_region_scope qa_native_region_scope;
typedef struct qa_native_region_snapshot qa_native_region_snapshot;
typedef enum qa_native_region_scope_phase {
    QA_NATIVE_SCOPE_TARGET_ENTRY,
    QA_NATIVE_SCOPE_FRAME_ENTRY,
    QA_NATIVE_SCOPE_REGION_ENTER,
    QA_NATIVE_SCOPE_REGION_JOIN
} qa_native_region_scope_phase;
typedef struct qa_native_region_scope_event {
    qa_native_region_scope *scope;
    qa_native_declared_region region;
    qa_native_region_scope_phase phase;
    qa_native_processor_state state;
} qa_native_region_scope_event;
typedef enum qa_native_region_scope_action {
    QA_NATIVE_SCOPE_CONTINUE,
    QA_NATIVE_SCOPE_SKIP_TO_ENTRY,
    QA_NATIVE_SCOPE_SKIP_TO_JOIN,
    QA_NATIVE_SCOPE_SKIP_TO_FRAME_EXIT
} qa_native_region_scope_action;
typedef struct qa_native_region_scope_decision {
    qa_native_region_scope_action action;
    bool replace_state;
    qa_native_processor_state state;
    const qa_native_region_snapshot *restore;
} qa_native_region_scope_decision;
typedef bool (*qa_native_region_scope_fn)(void *, qa_native_instance *,
    const qa_native_region_scope_event *, qa_native_region_scope_decision *, qa_error *);

/* Acquired declaration identity and executable intervals are qualified before
 * any scope is attached. The actual configured stopped CPU owns these slices;
 * the supplied target is invoked once using its real fixed ABI. */
bool qa_native_region_scope_open(qa_native_instance *, const qa_native_declaration *,
    uint32_t region_id, qa_native_address target, qa_native_region_scope_fn, void *,
    qa_native_region_scope **, qa_error *);
bool qa_native_region_scope_invoke(qa_native_region_scope *, const qa_native_signature *,
    const qa_native_value *, size_t, qa_native_value *, bool *entered, qa_error *);
bool qa_native_region_scope_argument_bytes(const qa_native_signature *, size_t *, qa_error *);
bool qa_native_region_scope_current(const qa_native_region_scope *);
/* Snapshot storage belongs to the scope. It retains actual segments, x87,
 * MXCSR, SIMD and general registers; it has no portable checkpoint identity. */
bool qa_native_region_scope_capture(qa_native_region_scope *,
    const qa_native_region_scope_event *, qa_native_region_snapshot **, qa_error *);
/* Commit reached input writes immediately. Other full CPU fields and the
 * instruction pointer retain their actual stopped values. */
bool qa_native_region_scope_write(qa_native_region_scope *,
    const qa_native_region_scope_event *, const qa_native_processor_state *, qa_error *);
bool qa_native_region_scope_restore(const qa_native_region_snapshot *,
    const qa_native_region_scope_event *, qa_native_region_scope_decision *, qa_error *);
/* Refusal leaves the scope and all snapshot/callback holdings reachable. */
bool qa_native_region_scope_close(qa_native_region_scope **, qa_error *);
#endif
