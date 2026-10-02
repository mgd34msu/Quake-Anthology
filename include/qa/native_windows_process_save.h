#ifndef QA_NATIVE_WINDOWS_PROCESS_SAVE_H
#define QA_NATIVE_WINDOWS_PROCESS_SAVE_H

#include "qa/native_windows_process.h"

typedef struct qa_native_windows_process_restore_bindings {
    size_t maximum_backing_bytes;
    qa_native_guest_backend backend;
    const char *host_executable; /* Borrowed actual child bootstrap only. */
    const guest_profile_guard_launch *profile_guard; /* Borrowed actual monitor launch only. */
    qa_native_windows_capabilities capabilities;
    qa_native_guest_callback_resolve_fn external_callback;
    void *external_context;
} qa_native_windows_process_restore_bindings;

/* Captures the full ordered source artifact graph, attachment ownership,
 * runtime/OS resources and actual lower CPU/RAM continuation at an idle boundary. */
bool qa_native_windows_process_checkpoint(qa_native_windows_process *, qa_buffer *, qa_error *);
/* Decode an inert candidate. The actual saved IAT/RAM/CPU/TLS and initializer
 * ordering remain authoritative; no image bind, service install, source call,
 * fresh CPU initialization or file opening occurs. Failed cleanup can retain
 * the candidate and all callback contexts in the output. */
bool qa_native_windows_process_restore(qa_bytes,
    const qa_native_windows_process_restore_bindings *, qa_native_windows_process **, qa_error *);
/* Final pure close-ownership transfer. NULL previous requires the actual outer
 * graph to transfer exclusive capability ownership. Previous stays disposable. */
bool qa_native_windows_process_adopt(qa_native_windows_process *,
    qa_native_windows_process *, qa_error *);
/* The candidate's actual external graph already holds independent native
 * references. Adopt its own close authority and retire the previous source
 * without transferring or closing the previous graph's references. */
bool qa_native_windows_process_adopt_owned(qa_native_windows_process *,
    qa_native_windows_process *, qa_error *);

#endif
