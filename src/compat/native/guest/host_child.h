#ifndef QA_NATIVE_GUEST_HOST_CHILD_H
#define QA_NATIVE_GUEST_HOST_CHILD_H

#include "host_memory.h"
#include "host_x86_64.h"
#include "profile/guard.h"
#include "profile/cpu.h"

typedef struct guest_host_child guest_host_child;
typedef struct guest_host_child_options {
    const char *executable; /* Actual controller executable with bootstrap join. */
    qa_native_target target;
    const guest_profile_guard_launch *profile_guard; /* Borrowed only during create. */
} guest_host_child_options;
typedef enum guest_host_stop_kind {
    GUEST_HOST_STOP_IMPORT, GUEST_HOST_STOP_RETURN, GUEST_HOST_STOP_FAULT,
    GUEST_HOST_STOP_SYSCALL, GUEST_HOST_STOP_OBSERVATION
} guest_host_stop_kind;
typedef struct guest_host_stop {
    guest_host_stop_kind kind;
    uint64_t callback_id, address;
    int signal_number, signal_code;
    uint32_t access;
    guest_profile_guard_fault profile;
    guest_host_x86_64_state state;
} guest_host_stop;
typedef bool (*guest_host_import_fn)(void *, guest_host_child *, uint64_t,
    guest_host_x86_64_state *, qa_error *);
typedef bool (*guest_host_observe_fn)(void *, guest_host_child *,
    const guest_profile_guard_fault *, qa_error *);
typedef bool (*guest_host_cancel_fn)(void *, const qa_error *);
typedef struct guest_host_observation {
    guest_host_observe_fn invoke;
    guest_host_cancel_fn cancelled;
    void *context;
    uint64_t bypass;
} guest_host_observation;
typedef struct guest_host_syscall {
    uint64_t instruction, next_instruction, number, arguments[6];
} guest_host_syscall;
typedef struct guest_host_syscall_result {
    int64_t value;
    bool stop; /* Actual source exit completes this invocation without resume. */
} guest_host_syscall_result;
typedef bool (*guest_host_syscall_fn)(void *, guest_host_child *,
    const guest_host_syscall *, guest_host_syscall_result *, qa_error *);

bool guest_host_child_create(const guest_host_child_options *, guest_host_child **, qa_error *);
bool guest_host_child_idle(const guest_host_child *);
bool guest_host_child_destroy(guest_host_child **, qa_error *);
const guest_host_x86_64_capabilities *guest_host_child_capability(const guest_host_child *);
/* Actual monitor-installation receipt from the physical child's PROBE. Paths
 * and artifact bytes cannot substitute for this receipt; source CPU-state
 * qualification remains a separate required boundary. */
bool guest_host_child_profile_read(const guest_host_child *, guest_profile_guard_receipt *, qa_error *);
bool guest_host_child_source_domain(const guest_host_child *, guest_profile_cpu_domain *, qa_error *);
/* Actual child PID/TID observed in its authenticated READY, distinct from any
 * source kernel's retained logical task IDs across a cold replacement. */
bool guest_host_child_process_read(const guest_host_child *, uint64_t *, uint64_t *, qa_error *);
/* Observe the actual authenticated child while stopped, including an entered
 * import/syscall callback. IDs 2/3 select its process/thread CPU clocks. The
 * source kernel owns elapsed accounting across replacement children. */
bool guest_host_child_cpu_clock_read(guest_host_child *, int32_t, int64_t *, int32_t *, qa_error *);
/* Called only by actual fresh source construction before its first invocation.
 * A restored source supplies its retained named CPU instead. */
bool guest_host_child_source_initialize(guest_host_child *, qa_error *);
bool guest_host_child_backing(guest_host_child *, const guest_host_backing_view *, qa_error *);
size_t guest_host_child_backing_count(const guest_host_child *);
/* Borrowed authoritative RAM remains owned until backing_remove or child
 * destroy. Borrow only while the actual child is stopped, including imports. */
bool guest_host_child_backing_at(const guest_host_child *, size_t, guest_host_backing_view *, qa_error *);
bool guest_host_child_backing_remove(guest_host_child *, uint64_t, qa_error *);
bool guest_host_child_map(guest_host_child *, const qa_native_guest_mapping *, qa_error *);
bool guest_host_child_change(guest_host_child *, const qa_native_guest_mapping *, uint32_t,
    bool, qa_error *);
bool guest_host_child_bind(guest_host_child *, uint64_t, uint64_t, qa_error *);
bool guest_host_child_unbind(guest_host_child *, uint64_t, qa_error *);
bool guest_host_child_interest(guest_host_child *, const guest_profile_interest *, bool, qa_error *);
bool guest_host_child_cpu_read(const guest_host_child *, guest_host_x86_64_state *, qa_error *);
bool guest_host_child_cpu_write(guest_host_child *, const guest_host_x86_64_state *, qa_error *);
bool guest_host_child_read(const guest_host_child *, uint64_t, void *, size_t, qa_error *);
bool guest_host_child_write(guest_host_child *, uint64_t, qa_bytes, qa_error *);
/* This is an explicitly unbudgeted native invocation. It cannot substitute
 * for qa_native_guest_run's instruction-budget/store-observer contract. Imports
 * carry genuine saved IDs and stopped ABI state; nested calls use the same
 * physical child. The actual ABI owner restores its enclosing CPU on success. */
bool guest_host_child_run(guest_host_child *, uint64_t, uint64_t,
    guest_host_import_fn, void *, qa_error *);
/* entered is set only by an authenticated, admitted source stop (including
 * its fault or actual return), before any import callback. A later failure
 * keeps it true; construction/preflight or rejected HOST_RUN keeps it false. */
bool guest_host_child_run_receipt(guest_host_child *, uint64_t, uint64_t,
    guest_host_import_fn, void *, const guest_host_observation *, bool *entered, qa_error *);
/* Explicit PROGRAM invocation capability. A real two-byte SYSCALL is stopped
 * before execution. On successful service return commit RAX, RCX, R11 and the
 * observed next PC; failure retains entry state. stop=true ends the entered
 * scope without resuming source code and sets program_stopped. The ordinary
 * library run does not admit raw syscalls. A zero return trap means the raw
 * PROGRAM has no controller return continuation; only its source exit stops. */
bool guest_host_child_run_with_syscalls(guest_host_child *, uint64_t, uint64_t,
    guest_host_import_fn, guest_host_syscall_fn, void *, const guest_host_observation *, bool *program_stopped, qa_error *);
bool guest_host_child_last_fault(const guest_host_child *, guest_host_stop *, qa_error *);
/* Called by the real executable startup before ordinary application creation.
 * A matching child command runs its private server and never enters app Init. */
bool guest_host_child_bootstrap(int, char *const *, bool *, int *, qa_error *);

#endif
