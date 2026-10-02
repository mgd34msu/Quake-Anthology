#ifndef QA_NATIVE_PROCESS_H
#define QA_NATIVE_PROCESS_H

#include "qa/native_sysv_process_save.h"
#include "qa/native_windows_process_save.h"

typedef enum qa_native_process_kind {
    QA_NATIVE_PROCESS_SYSV, QA_NATIVE_PROCESS_WINDOWS
} qa_native_process_kind;
struct qa_native_process_resources;

/* The enclosing application supplies its actual prepared artifact/resource
 * graph. Source identity selects a row in that graph, never a synthetic image.
 * Callback IDs occupy a caller-assigned unused lower namespace. */
typedef struct qa_native_process_options {
    qa_native_process_kind kind;
    uint64_t source_id, first_callback;
    union {
        const qa_native_sysv_process_options *sysv;
        const qa_native_windows_process_options *windows;
    } fresh;
    /* Nonempty continuation selects detached restoration instead of fresh
     * loading. Bindings borrow already prepared durable capabilities. */
    qa_bytes continuation;
    union {
        const qa_native_sysv_process_restore_bindings *sysv;
        const qa_native_windows_process_restore_bindings *windows;
    } restored;
    qa_native_instance *previous; /* Retained source resource owner, if present. */
    struct qa_native_process_resources *resources;
} qa_native_process_options;

bool qa_native_process_checkpoint(qa_native_instance *, qa_buffer *, qa_error *);

#endif
