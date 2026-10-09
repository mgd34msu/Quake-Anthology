#ifndef QA_NATIVE_SYSV_PROCESS_SAVE_H
#define QA_NATIVE_SYSV_PROCESS_SAVE_H

#include "qa/native_sysv_process.h"

typedef struct qa_native_sysv_process_restore_bindings {
    const qa_native_sysv_artifact *artifacts; /* Installed, borrowed through restore. */
    size_t artifact_count;
    size_t maximum_backing_bytes;
    qa_native_guest_backend backend;
    /* Actual child bootstrap, borrowed through restore only, never saved. */
    const char *host_executable;
    /* Actual installed-monitor launch, borrowed through lower restoration
     * only. Its saved policy is qualified by the real child, never by a path. */
    const guest_profile_guard_launch *profile_guard;
    uint64_t clock_id;
    bool output_is_terminal;
    bool (*time)(void *, int64_t *, qa_error *);
    bool (*random)(void *, void *, size_t, uint32_t, size_t *, int32_t *, qa_error *);
    bool (*clock)(void *, int32_t, int64_t *, int32_t *, qa_error *);
    bool (*calendar)(void *, int64_t, bool, qa_platform_calendar_fields *, qa_error *);
    bool (*getcwd)(void *, qa_buffer *, qa_error *);
    bool (*current)(void *, qa_error *);
    void *context;
    /* Borrows an existing capability by its durable identity. Only its
     * capability/mode/callback/context fields are consumed. Never reopen,
     * create, truncate, seek or otherwise replay the saved opening. */
    bool (*file)(void *, uint64_t, qa_native_sysv_file *, qa_error *);
    /* Rebinds the actual opener for future source calls; restore never opens. */
    bool (*open_file)(void *, const char *, uint32_t, uint32_t,
                      qa_native_sysv_file *, bool *, qa_error *);
    bool (*open_temporary_file)(void *, qa_native_sysv_file *, bool *, qa_error *);
    void *file_context;
    /* Resolves actual saved source-bridge callback IDs outside the runtime
     * import registry. Descriptor or address faults never fall through. */
    qa_native_guest_callback_resolve_fn external_callback;
    void *external_context;
} qa_native_sysv_process_restore_bindings;

/* Nests the exact backend, CPU/RAM/allocator/import runtime, immutable
 * artifact identities and their ordered profile provenance, loaded-image backing
 * ownership and external resource cursors.
 * Outer envelope, artifact and detached runtime/resource admission precede
 * the pure borrowed capability resolver. Lower CPU and loaded-image ownership
 * validation then complete before capability adoption or any source call. */
bool qa_native_sysv_process_checkpoint(qa_native_sysv_process *, qa_buffer *, qa_error *);
/* Creates an inert provisional candidate. No image loading, relocations,
 * IFUNCs, constructors, fresh processor-state initialization, stream I/O or
 * file opening occurs. The lower CPU reconstructs its exact saved state.
 * A failure may retain an owner when real CPU retirement refuses cleanup. */
bool qa_native_sysv_process_restore(qa_bytes,
    const qa_native_sysv_process_restore_bindings *, qa_native_sysv_process **, qa_error *);
/* Pure capability close-ownership transfer after complete restoration. NULL
 * previous requires exclusive ownership transferred by the actual external
 * resource graph. Previous remains retained for checked disposal. This is
 * the final publication boundary; no callback, allocation or I/O occurs. */
bool qa_native_sysv_process_adopt(qa_native_sysv_process *, qa_native_sysv_process *, qa_error *);
/* The actual external graph supplied independent native holds and close
 * ownership for the candidate. Retire the previous graph separately. */
bool qa_native_sysv_process_adopt_owned(qa_native_sysv_process *, qa_native_sysv_process *, qa_error *);

#endif
