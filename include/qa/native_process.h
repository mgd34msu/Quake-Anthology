#ifndef QA_NATIVE_PROCESS_H
#define QA_NATIVE_PROCESS_H

#include "qa/native_sysv_process_save.h"
#include "qa/native_windows_process_save.h"
#include "qa/filesystem.h"

typedef enum qa_native_process_kind {
    QA_NATIVE_PROCESS_SYSV, QA_NATIVE_PROCESS_WINDOWS
} qa_native_process_kind;
/* Borrowed services from the actual prepared resource owner. A nonempty
 * context transfers one retained reference into the source instance. Checked
 * release must preserve any remaining close custody; root/open services admit
 * real source save files without an application dependency in the ABI helper. */
typedef struct qa_native_process_resource_services {
    void *context;
    void (*retain)(void *);
    bool (*release)(void **, qa_error *);
    bool (*root_add)(void *, const char *, qa_fs_root *, uint32_t, uint64_t *, qa_error *);
    bool (*root_remove)(void *, uint64_t, qa_error *);
    bool (*open_windows_file)(void *, const char *, uint32_t, qa_fs_opened_creation,
        qa_native_windows_file *, bool *, qa_error *);
    bool (*open_sysv_file)(void *, const char *, uint32_t, qa_fs_opened_creation,
        qa_native_sysv_file *, bool *, qa_error *);
} qa_native_process_resource_services;

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
    /* Stage original CPU/RAM first; the actual host decodes canonical actors
     * after its enclosing world foundation has been restored. Source calls
     * remain fenced until restore_host commits the retained HOST capsule. */
    bool defer_host_restore;
    union {
        const qa_native_sysv_process_restore_bindings *sysv;
        const qa_native_windows_process_restore_bindings *windows;
    } restored;
    qa_native_instance *previous; /* Retained source resource owner, if present. */
    qa_native_process_resource_services resources;
} qa_native_process_options;

bool qa_native_process_checkpoint(qa_native_instance *, qa_buffer *, qa_error *);
/* The enclosing decoded HOST must exactly match the retained process capsule.
 * This commits the real actor decoder and independent capability adoption;
 * any postmutation failure makes the candidate terminal. */
bool qa_native_process_restore_host(qa_native_instance *, qa_bytes expected_host, qa_error *);
bool qa_native_process_restore_pending(const qa_native_instance *);

#endif
