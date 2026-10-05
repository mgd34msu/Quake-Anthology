#ifndef QA_NATIVE_SYSV_PROGRAM_H
#define QA_NATIVE_SYSV_PROGRAM_H

#include "qa/native_sysv_process.h"
#include "qa/native_process_platform.h"

typedef struct qa_native_sysv_program qa_native_sysv_program;
typedef struct qa_native_sysv_program_aux { uint64_t tag, value; } qa_native_sysv_program_aux;
typedef struct qa_native_sysv_program_descriptor_status {
    uint32_t flags;
    int64_t offset;
    bool seekable;
} qa_native_sysv_program_descriptor_status;
/* These are held kernel capabilities, independent of a userspace libc runtime.
 * Opening transfers a real close owner even on failure when opened is true.
 * Restore uses resolve_file only, and never reopens an original pathname. */
typedef struct qa_native_sysv_program_services {
    uint64_t id;
    bool (*current)(void *, qa_error *);
    bool (*open_file)(void *, const char *, uint32_t, uint32_t,
        qa_native_sysv_file *, bool *, qa_error *);
    bool (*resolve_file)(void *, uint64_t, qa_native_sysv_file *, qa_error *);
    /* Rebuild private mapped pages from the same contained resource name even
     * after its source descriptor closed. The result owns its temporary read
     * lease until pristine.release, independently of descriptor close state. */
    bool (*file_baseline)(void *, uint64_t, const qa_native_guest_file *,
        qa_native_guest_pristine *, qa_error *);
    bool (*file_status)(void *, uint64_t, qa_fs_posix_status *, qa_error *);
    /* Actual native status flags/current position at acquisition. ESPIPE is a
     * successful nonseekable receipt; cold keeps its named logical position. */
    bool (*descriptor_status)(void *, uint64_t,
        qa_native_sysv_program_descriptor_status *, qa_error *);
    /* Reached kernel F_SETFL/IO operation, not a cold installer. Preserve real
     * native access/containment flags while applying source APPEND/NONBLOCK. */
    bool (*descriptor_flags)(void *, uint64_t, uint32_t, qa_error *);
    bool (*identity)(void *, qa_native_process_linux_identity *, qa_error *);
    bool (*entropy)(void *, void *, size_t, qa_error *);
    /* Actual seconds and nanoseconds for the requested Linux clock ID. */
    bool (*clock)(void *, int32_t, int64_t *, int32_t *, qa_error *);
    /* Immediate pure receipt from the last failed actual native operation. */
    bool (*native_error)(const void *, qa_fs_native_error *);
    void *context;
} qa_native_sysv_program_services;
typedef struct qa_native_sysv_program_options {
    qa_native_guest_options guest;
    qa_native_sysv_artifact program, interpreter;
    const char *interpreter_path, *executed_path, *platform, *base_platform;
    size_t stack_bytes, instruction_budget;
    uint32_t anonymous_permissions;
    bool read_implies_execute;
    const char *const *argv, *const *environment;
    size_t argc, environment_count;
    qa_bytes random;
    const qa_native_sysv_program_aux *auxiliary;
    size_t auxiliary_count;
    qa_native_sysv_file standard_files[3];
    /* EMULATED IDs are issued by the actual enclosing kernel namespace. HOST
     * fresh construction obtains its physical child's IDs when these are zero.
     * Cold source IDs remain the saved kernel IDs despite new physical plumbing. */
    uint64_t process_id, thread_id;
    qa_native_sysv_program_services services;
} qa_native_sysv_program_options;
typedef struct qa_native_sysv_program_status {
    uint64_t process_id, thread_id, clear_child_tid, robust_list;
    uint32_t exit_code;
    bool exited;
} qa_native_sysv_program_status;

/* Owns raw PROGRAM-role mappings, selected interpreter, initial stack, kernel
 * descriptors and source scheduling. No library relocation/IFUNC/initializer
 * or stock runtime installation occurs. Failure retains partial close owners. */
bool qa_native_sysv_program_create(const qa_native_sysv_program_options *,
    qa_native_sysv_program **, qa_error *);
qa_native_guest *qa_native_sysv_program_guest(const qa_native_sysv_program *);
bool qa_native_sysv_program_status_read(const qa_native_sysv_program *,
    qa_native_sysv_program_status *, qa_error *);
/* Enters the retained current PC with the genuine invocation-scoped syscall
 * owner. A source exit retains its stopped CPU and exact exit status. */
bool qa_native_sysv_program_run(qa_native_sysv_program *, qa_error *);
bool qa_native_sysv_program_dispose(qa_native_sysv_program **, qa_error *);

#endif
