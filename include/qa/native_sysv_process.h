#ifndef QA_NATIVE_SYSV_PROCESS_H
#define QA_NATIVE_SYSV_PROCESS_H

#include "qa/native_guest.h"
#include "qa/platform_services.h"

typedef struct qa_native_sysv_process qa_native_sysv_process;
typedef enum qa_native_sysv_role {
    QA_NATIVE_SYSV_LIBRARY, QA_NATIVE_SYSV_PROGRAM
} qa_native_sysv_role;
typedef struct qa_native_sysv_artifact {
    uint64_t provider, load_bias;
    qa_native_sysv_role role;
    qa_native_image_info image;
    qa_bytes bytes;
    size_t maximum_image_bytes;
} qa_native_sysv_artifact;
enum { QA_NATIVE_SYSV_FILE_READ = 1, QA_NATIVE_SYSV_FILE_WRITE = 2 };
/* Already opened capabilities. Registration transfers their close ownership;
 * callbacks report actual completed prefixes even on failure. Nothing opens
 * a path, creates, truncates or seeks an external object during construction. */
typedef struct qa_native_sysv_file {
    uint64_t handle, capability;
    const char *name;
    uint32_t mode, creation;
    bool (*read)(void *, uint64_t, void *, size_t, size_t *, qa_error *);
    bool (*write)(void *, uint64_t, qa_bytes, size_t *, qa_error *);
    bool (*size)(void *, uint64_t *, qa_error *);
    bool (*truncate)(void *, uint64_t, qa_error *);
    bool (*flush)(void *, qa_error *);
    bool (*close)(void *, qa_error *);
    void *context;
} qa_native_sysv_file;
typedef struct qa_native_sysv_process_options {
    qa_native_guest_options guest;
    const qa_native_sysv_artifact *artifacts;
    size_t artifact_count;
    uint64_t scope, first_function, trap_base;
    /* Positive budget for EMULATED; exactly zero for explicit HOST calls. */
    size_t trap_bytes, stack_bytes, instruction_budget;
    uint32_t anonymous_permissions;
    bool read_implies_execute;
    const char *const *argv, *const *environment;
    size_t argc, environment_count;
    const qa_native_sysv_file *files;
    size_t file_count;
    /* A source fopen may acquire a contained file through the actual prepared
     * authority. opened=true transfers ownership even on a false result. */
    bool (*open_file)(void *, const char *, uint32_t, uint32_t,
                      qa_native_sysv_file *, bool *, qa_error *);
    bool (*open_temporary_file)(void *, qa_native_sysv_file *, bool *, qa_error *);
    void *file_context;
    uint64_t standard_handles[3], clock_id;
    bool output_is_terminal;
    bool (*time)(void *, int64_t *, qa_error *);
    bool (*random)(void *, void *, size_t, uint32_t, size_t *, int32_t *, qa_error *);
    bool (*clock)(void *, int32_t, int64_t *, int32_t *, qa_error *);
    bool (*calendar)(void *, int64_t, bool, qa_platform_calendar_fields *, qa_error *);
    /* Owned UTF-8 path: size excludes its trailing NUL. */
    bool (*getcwd)(void *, qa_buffer *, qa_error *);
    /* Qualifies the actual prepared external resource/clock authority. It
     * must work before a source instance is constructed or published. */
    bool (*current)(void *, qa_error *);
    void *context;
} qa_native_sysv_process_options;

/* Owns distinct artifact copies, one real guest, stack/return continuation,
 * runtime and external resource graph. Artifact order is the caller's actual
 * prepared dependency order. Relocation may invoke genuine IFUNC resolvers;
 * lifecycle initialization is explicit. This API does not enter a program's
 * interpreter or construct a kernel auxv from guessed process metadata. */
/* HOST requires guest.profile_guard for the actual installed instruction
 * monitor before loading can invoke IFUNCs. The borrowed launch is consumed
 * during lower construction only; backend identity alone is not admission. */
bool qa_native_sysv_process_create(const qa_native_sysv_process_options *,
    qa_native_sysv_process **, qa_error *);
bool qa_native_sysv_process_idle(const qa_native_sysv_process *);
qa_native_guest *qa_native_sysv_process_guest(const qa_native_sysv_process *);
bool qa_native_sysv_process_initialize(qa_native_sysv_process *, uint64_t, qa_error *);
bool qa_native_sysv_process_finalize(qa_native_sysv_process *, uint64_t, qa_error *);
bool qa_native_sysv_process_finalize_destructors(qa_native_sysv_process *, uint64_t, qa_error *);
bool qa_native_sysv_process_finalize_all(qa_native_sysv_process *, qa_error *);
/* Run the actual module finalizers, replace its physical image attachment,
 * relocate and initialize it in the retained process/runtime/resource graph.
 * Any failure after entry requires whole-process retirement. */
bool qa_native_sysv_process_reload(qa_native_sysv_process *, uint64_t, qa_error *);
/* Borrowed original bytes remain owned by this process until disposal. */
bool qa_native_sysv_process_artifact_read(const qa_native_sysv_process *, uint64_t,
    qa_native_sysv_artifact *, qa_error *);
/* Transfers an actually opened capability at the idle boundary, for example
 * an original module's explicit WriteGame/ReadGame temporary file. */
bool qa_native_sysv_process_file_add(qa_native_sysv_process *, const qa_native_sysv_file *, qa_error *);
bool qa_native_sysv_process_file_close(qa_native_sysv_process *, uint64_t, qa_error *);
/* Forget only a successfully closed temporary capability. */
bool qa_native_sysv_process_file_remove(qa_native_sysv_process *, uint64_t, qa_error *);
/* Actual fixed/POD signature only. Variadic calls need their own explicit
 * promoted-layout bridge; argument count never supplies an inferred ABI.
 * A genuine stopped import callback may invoke source recursively; the outer
 * process operation and its checked disposal scope remain held throughout. */
bool qa_native_sysv_process_invoke(qa_native_sysv_process *, uint64_t,
    const qa_native_signature *, const qa_native_value *, size_t,
    qa_native_value *, qa_error *);
/* Executes the genuine observed entry once through its actual EMULATED
 * instruction/store scope; recursive entries remain intercepted. */
bool qa_native_sysv_process_invoke_original(qa_native_sysv_process *, uint64_t, uint64_t,
    const qa_native_signature *, const qa_native_value *, size_t,
    qa_native_value *, qa_error *);
bool qa_native_sysv_process_export(const qa_native_sysv_process *, uint64_t,
    const char *, const char *, uint64_t *, qa_error *);
/* Disposal runs no source callbacks or destructors. Checked external close
 * or CPU-close refusal retains the complete owner and callback contexts. */
bool qa_native_sysv_process_dispose(qa_native_sysv_process **, qa_error *);

#endif
