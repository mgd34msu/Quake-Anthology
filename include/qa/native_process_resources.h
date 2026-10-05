#ifndef QA_NATIVE_PROCESS_RESOURCES_H
#define QA_NATIVE_PROCESS_RESOURCES_H

#include "qa/launch.h"
#include "qa/native_process.h"
#include "qa/native_process_platform.h"
#include "qa/native_runtime.h"
#include "qa/native_sysv_program_save.h"

typedef struct qa_native_process_resources qa_native_process_resources;
typedef struct qa_native_process_resource_artifact {
    const qa_resource *resource;
    const qa_vfs_acquisition *acquisition;
    const char *path;
} qa_native_process_resource_artifact;
typedef struct qa_native_process_resource_policy {
    qa_native_guest_backend backend;
    size_t maximum_image_bytes, maximum_backing_bytes, stack_bytes;
    size_t instruction_budget, runtime_trap_bytes;
} qa_native_process_resource_policy;
/* Explicit source-visible path prefix bound to a genuinely acquired directory
 * authority. Prefixes are matched in this order; no host path or stock library
 * directory is derived from a source import name. */
typedef struct qa_native_process_resource_root {
    const char *prefix;
    qa_fs_root *root;
    uint32_t mode;
} qa_native_process_resource_root;
/* Exact acquired order, including the selected primary. Implementation-owned
 * addresses/callback IDs are issued beneath this genuine service owner. The
 * current callback qualifies the original prepared/live application row and
 * must already work before any source constructor executes. */
typedef struct qa_native_process_resources_options {
    const qa_launch_instance *descriptor;
    const qa_native_process_resource_artifact *artifacts;
    size_t artifact_count, primary;
    qa_actor_owner receiver;
    uint64_t service_owner;
    qa_native_process_resource_policy policy;
    qa_native_runtime *runtime;
    const char *bootstrap;
    qa_native_process_platform *platform;
    const qa_native_process_resource_root *roots;
    size_t root_count;
    const char *const *argv, *const *environment;
    size_t argc, environment_count;
    const uint16_t *command_line, *windows_environment;
    size_t command_line_units, windows_environment_units;
    qa_native_guest_callback_resolve_fn external_callback;
    void *external_context;
    bool (*current)(void *, const qa_launch_instance *, qa_actor_owner, uint64_t, qa_error *);
    void *context;
} qa_native_process_resources_options;
typedef struct qa_native_process_resource_program {
    bool has_interpreter, read_implies_execute;
    size_t interpreter;
    const char *interpreter_path; /* Exact PT_INTERP acquisition request. */
    uint32_t anonymous_permissions;
    uint64_t process_id, thread_id;
    const char *platform, *base_platform;
    qa_bytes random; /* Actual acquired 16 bytes; no cold entropy replay. */
    const qa_native_sysv_program_aux *auxiliary;
    size_t auxiliary_count;
} qa_native_process_resource_program;

bool qa_native_process_resources_create(const qa_native_process_resources_options *,
    qa_native_process_resources **, qa_error *);
/* Reconstruct a file-cold recipe against the genuinely prepared artifact/root
 * authorities. Named objects are opened without creation or truncation through
 * the current contained root/name. Standard roles use actual platform bindings.
 * Failed import may return an owner which requires checked release. */
bool qa_native_process_resources_restore(const qa_native_process_resources_options *,
    qa_bytes, qa_native_process_resources **, qa_error *);
/* Dedicated raw ELF PROGRAM graph: exactly the acquired executable and its
 * declared PT_INTERP artifact, if present. Scalar personality/auxiliary values
 * come from the actual kernel/source policy, not a host library lookup. */
bool qa_native_process_resources_program_create(const qa_native_process_resources_options *,
    const qa_native_process_resource_program *, qa_native_process_resources **, qa_error *);
/* File-cold PROGRAM import against the current executable/interpreter and
 * contained roots. The same resource recipe codec restores logical files;
 * no program startup instructions or original pathname authority are replayed. */
bool qa_native_process_resources_program_restore(const qa_native_process_resources_options *,
    const qa_native_process_resource_program *, qa_bytes,
    qa_native_process_resources **, qa_error *);
bool qa_native_process_resources_program_read(qa_native_process_resources *,
    qa_native_sysv_program_options *, qa_error *);
bool qa_native_process_resources_program_restore_read(qa_native_process_resources *,
    qa_native_sysv_program_restore_bindings *, qa_error *);
bool qa_native_process_resources_current(const qa_native_process_resources *, qa_error *);
/* Immediate same-thread operation receipt. available=false means a framework
 * admission failure, not an invented OS errno. No operation or source callback
 * may intervene between the failed capability call and this pure read. */
bool qa_native_process_resources_native_error_read(const qa_native_process_resources *,
    qa_fs_native_error *);
/* Options borrow this owner through source construction only. Source process
 * destruction must finish before the last resource-owner release. */
bool qa_native_process_resources_options_read(qa_native_process_resources *,
    qa_native_process_options *, qa_error *);
bool qa_native_process_resources_restore_read(qa_native_process_resources *,
    qa_bytes continuation, qa_native_instance *previous, qa_native_process_options *, qa_error *);
bool qa_native_process_resources_checkpoint(const qa_native_process_resources *, qa_buffer *, qa_error *);
/* A capture owns independent logical capability rows and holds the same native
 * objects, so subsequent source closes do not destroy the captured graph.
 * Rebind copies those held rows into an inert candidate under the actual new
 * prepared authority. Captures cannot supply execution options and never call
 * their retired source context. Neither operation opens files or runs source
 * code. */
bool qa_native_process_resources_capture(const qa_native_process_resources *,
    qa_native_process_resources **, qa_buffer *, qa_error *);
bool qa_native_process_resources_rebind(const qa_native_process_resources *,
    const qa_native_process_resources_options *, qa_bytes,
    qa_native_process_resources **, qa_error *);
/* Pure match against the retained acquired graph, namespace and platform
 * owner. Never acquires an artifact or reopens/duplicates a capability. */
bool qa_native_process_resources_validate(const qa_native_process_resources *, qa_bytes, qa_error *);
/* Registers an actual contained stream before a source Write/Read lifecycle
 * operation. A genuine miss returns true with opened=false. Resolve borrows
 * the retained object; it never opens the path. A physically consumed failed
 * close retains only its idempotent close receipt; its data operations fail.
 * Captured/rebound graphs own independent native-object holds: their process
 * resource adoption must not transfer a previous graph's callback contexts. */
bool qa_native_process_resources_open_file(qa_native_process_resources *, const char *, uint32_t,
    qa_fs_opened_creation, qa_native_windows_file *, bool *, qa_error *);
bool qa_native_process_resources_resolve_file(qa_native_process_resources *, uint64_t,
    qa_native_windows_file *, qa_error *);
bool qa_native_process_resources_open_sysv_file(qa_native_process_resources *, const char *, uint32_t,
    qa_fs_opened_creation, qa_native_sysv_file *, bool *, qa_error *);
bool qa_native_process_resources_resolve_sysv_file(qa_native_process_resources *, uint64_t,
    qa_native_sysv_file *, qa_error *);
bool qa_native_process_resources_file_status(qa_native_process_resources *, uint64_t,
    qa_fs_posix_status *, qa_error *);
bool qa_native_process_resources_descriptor_status(qa_native_process_resources *, uint64_t,
    qa_native_sysv_program_descriptor_status *, qa_error *);
/* Reached PROGRAM operation, never a cold decode/adoption side effect. */
bool qa_native_process_resources_descriptor_flags(qa_native_process_resources *, uint64_t,
    uint32_t linux_flags, qa_error *);
bool qa_native_process_resources_linux_identity_read(qa_native_process_resources *,
    qa_native_process_linux_identity *, qa_error *);
bool qa_native_process_resources_linux_clock_read(qa_native_process_resources *,
    int32_t, int64_t *, int32_t *, qa_error *);
/* Adds an actually opened temporary/source directory to this owner's path
 * namespace. Removal requires all live file rows for that root to be closed;
 * historical rows and retained captures keep their original root identity. */
bool qa_native_process_resources_root_add(qa_native_process_resources *,
    const qa_native_process_resource_root *, uint64_t *, qa_error *);
bool qa_native_process_resources_root_remove(qa_native_process_resources *, uint64_t, qa_error *);
void qa_native_process_resources_retain(qa_native_process_resources *);
bool qa_native_process_resources_release(qa_native_process_resources **, qa_error *);

#endif
