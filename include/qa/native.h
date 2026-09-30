#ifndef QA_NATIVE_H
#define QA_NATIVE_H

#include "qa/actors.h"
#include "qa/hash.h"
#include "qa/json.h"
#include "qa/qvm.h"

typedef struct qa_native_module qa_native_module;
typedef struct qa_native_instance qa_native_instance;
typedef struct qa_native_declaration qa_native_declaration;
typedef struct qa_native_region_binding qa_native_region_binding;

typedef uint64_t qa_native_address;

typedef enum qa_native_image_format {
    QA_NATIVE_IMAGE_PE32,
    QA_NATIVE_IMAGE_PE32_PLUS,
    QA_NATIVE_IMAGE_ELF32,
    QA_NATIVE_IMAGE_ELF64
} qa_native_image_format;

typedef enum qa_native_os {
    QA_NATIVE_OS_WINDOWS,
    QA_NATIVE_OS_LINUX,
    QA_NATIVE_OS_MACOS
} qa_native_os;

typedef enum qa_native_arch {
    QA_NATIVE_ARCH_I386,
    QA_NATIVE_ARCH_X86_64,
    QA_NATIVE_ARCH_AARCH64
} qa_native_arch;

typedef enum qa_native_abi {
    QA_NATIVE_ABI_CDECL_I386,
    QA_NATIVE_ABI_MICROSOFT_X64,
    QA_NATIVE_ABI_SYSTEM_V_I386,
    QA_NATIVE_ABI_SYSTEM_V_X64,
    QA_NATIVE_ABI_AAPCS64
} qa_native_abi;

typedef enum qa_native_profile {
    QA_NATIVE_Q2_GAME_API3,
    QA_NATIVE_Q2_GAME_API2023,
    QA_NATIVE_Q2_CGAME_API2023,
    QA_NATIVE_Q3_VMMAIN,
    QA_NATIVE_QUAKE_LIVE_GAME_API10
} qa_native_profile;

typedef struct qa_native_target {
    qa_native_os os;
    qa_native_arch arch;
    qa_native_abi abi;
    uint8_t pointer_bytes;
} qa_native_target;

typedef struct qa_native_image_info {
    qa_native_image_format format;
    qa_native_target target;
    uint64_t preferred_base;
    uint64_t image_bytes;
    qa_sha256_digest digest;
} qa_native_image_info;

/* Inspection never executes the artifact. PE and ELF structural metadata is
 * validated before a module is admitted to either a direct or runner backend.
 */
bool qa_native_inspect(qa_bytes image, qa_native_image_info *out, qa_error *error);
qa_native_target qa_native_host_target(void);

typedef struct qa_native_module_info {
    qa_native_profile profile;
    qa_native_image_info image;
    const char *source; /* Borrowed until the module is released. */
} qa_native_module_info;

/* Modules own immutable artifact bytes. Instances retain the module and load a
 * distinct image so file-scope guest globals are never shared accidentally. */
bool qa_native_module_load(qa_bytes image, const char *source, qa_native_profile profile,
                           const qa_sha256_digest *expected_digest, qa_native_module **out,
                           qa_error *error);
void qa_native_module_retain(qa_native_module *module);
void qa_native_module_release(qa_native_module *module);
qa_native_module_info qa_native_module_describe(const qa_native_module *module);

typedef enum qa_native_value_type {
    QA_NATIVE_VOID,
    QA_NATIVE_I8,
    QA_NATIVE_U8,
    QA_NATIVE_I16,
    QA_NATIVE_U16,
    QA_NATIVE_I32,
    QA_NATIVE_U32,
    QA_NATIVE_I64,
    QA_NATIVE_U64,
    QA_NATIVE_F32,
    QA_NATIVE_F64,
    QA_NATIVE_ADDRESS,
    QA_NATIVE_BYTES
} qa_native_value_type;

typedef struct qa_native_memory {
    void *data;
    size_t size;
} qa_native_memory;

typedef struct qa_native_value {
    qa_native_value_type type;
    union {
        int8_t i8;
        uint8_t u8;
        int16_t i16;
        uint16_t u16;
        int32_t i32;
        uint32_t u32;
        int64_t i64;
        uint64_t u64;
        float f32;
        double f64;
        qa_native_address address;
        qa_native_memory bytes; /* Borrowed for the duration of one callback. */
    } as;
} qa_native_value;

/* Byte aggregates describe their native fields recursively so the ABI can
 * classify register and stack passing correctly. count repeats one field;
 * scalar descriptors have no fields and a count of one. */
typedef struct qa_native_type {
    qa_native_value_type kind;
    const struct qa_native_type *fields;
    size_t field_count;
    size_t count;
} qa_native_type;

typedef struct qa_native_signature {
    qa_native_abi abi;
    const qa_native_type *parameters;
    size_t parameter_count;
    qa_native_type result;
    bool variadic;
} qa_native_signature;

typedef struct qa_native_import_call {
    qa_native_profile profile;
    uint32_t slot;
    const char *name; /* Borrowed static profile name. */
    const qa_native_signature *signature;
    const qa_native_value *arguments;
    size_t argument_count;
} qa_native_import_call;

typedef struct qa_native_dependency {
    const char *path; /* Single file name materialized beside the module. */
    qa_bytes bytes;
} qa_native_dependency;

/* Fixed ABI imports are decoded before dispatch. A byte result is initialized
 * to writable result storage of the declared size. Callers of an entry with a
 * byte result likewise initialize result.type, result.as.bytes.data and its
 * capacity before the call. All callback data expires when dispatch returns.
 * Failure is latched until the surrounding entry unwinds. */
typedef bool (*qa_native_import_fn)(void *context, qa_native_instance *instance,
                                    const qa_native_import_call *call, qa_native_value *result,
                                    qa_error *error);

/* Native Q3 syscalls are variadic. The description callback must provide the
 * exact argument types before any variadic argument is consumed. Returned
 * parameter storage remains valid until describe is called again. */
typedef bool (*qa_native_syscall_describe_fn)(void *context, int32_t service,
                                              const qa_native_value_type **types, size_t *count,
                                              qa_error *error);
typedef bool (*qa_native_syscall_fn)(void *context, qa_native_instance *instance, int32_t service,
                                     const qa_native_value *arguments, size_t argument_count,
                                     intptr_t *result, qa_error *error);

typedef bool (*qa_native_host_checkpoint_fn)(void *context, qa_buffer *state, qa_error *error);
typedef bool (*qa_native_host_restore_fn)(void *context, qa_bytes state, qa_error *error);

/* context and callback functions remain valid until instance destruction.
 * Dependency bytes and paths are borrowed only during creation. Declaration
 * identity and region records are copied into the instance. */
typedef struct qa_native_options {
    void *context;
    qa_qvm_role q3_role;
    qa_native_import_fn import;
    qa_native_syscall_describe_fn describe_syscall;
    qa_native_syscall_fn syscall;
    qa_native_host_checkpoint_fn checkpoint;
    qa_native_host_restore_fn restore;
    const qa_native_declaration *declaration;
    const qa_sha256_digest *declaration_digest;
    const qa_native_dependency *dependencies;
    size_t dependency_count;
    uint32_t tick_rate;
    float frame_seconds;
    uint32_t frame_milliseconds;
    /* Requires the matching instrumented helper even without inline regions. */
    bool observe;
} qa_native_options;

typedef enum qa_native_backend {
    QA_NATIVE_BACKEND_DIRECT,
    QA_NATIVE_BACKEND_RUNNER
} qa_native_backend;

/* Runner paths are explicit packaging inputs. A Windows target on a non-Windows
 * host requires wine; declared inline regions additionally require the matching
 * DynamoRIO launcher and client. Linux i386 targets use the Linux i386 pair.
 * Paths are borrowed only during creation. wine_drive defaults to "Z:" and is
 * the Wine mapping used to translate absolute host paths passed to drrun. The
 * helper isolates module memory and ABI state, but is not a security sandbox;
 * guest OS calls retain the helper account's authority. */
typedef struct qa_native_runner_config {
    const char *windows_i386_runner;
    const char *windows_x86_64_runner;
    const char *linux_i386_runner;
    const char *linux_x86_64_runner;
    const char *windows_i386_drrun;
    const char *windows_x86_64_drrun;
    const char *windows_i386_client;
    const char *windows_x86_64_client;
    const char *linux_i386_drrun;
    const char *linux_x86_64_drrun;
    const char *linux_i386_client;
    const char *linux_x86_64_client;
    const char *wine;
    const char *wine_drive;
    size_t maximum_frame_bytes;
} qa_native_runner_config;

typedef enum qa_native_lifecycle {
    QA_NATIVE_LOADED,
    QA_NATIVE_PREINITIALIZED,
    QA_NATIVE_INITIALIZED,
    QA_NATIVE_SHUT_DOWN
} qa_native_lifecycle;

/* Direct instances accept only the current process OS, architecture and ABI,
 * and reject declarations with inline regions. Guest code is trusted native
 * code and may compromise the process. Imports are synchronous. Activation is
 * stacked, so permitted nested calls restore the previous instance; callbacks
 * from module-created threads are rejected. */
bool qa_native_create_direct(qa_native_module *module, const qa_native_options *options,
                             qa_native_instance **out, qa_error *error);
bool qa_native_create_runner(qa_native_module *module, const qa_native_options *options,
                             const qa_native_runner_config *runner, qa_native_instance **out,
                             qa_error *error);
/* Selects direct execution for an exact process target and otherwise requires
 * runner configuration. It never falls back to emulation or a different ABI. */
bool qa_native_create(qa_native_module *module, const qa_native_options *options,
                      const qa_native_runner_config *runner, qa_native_instance **out,
                      qa_error *error);
/* Once can_destroy succeeds, destruction consumes either backend even if a
 * shutdown, unload callback or transport fault is returned. Admission rejection
 * leaves the caller's owner live. */
bool qa_native_destroy(qa_native_instance *instance, qa_error *error);
/* True only when destruction will pass its initial ownership admission.
 * Shutdown or runner cleanup may still fail after admission. */
bool qa_native_can_destroy(const qa_native_instance *instance);
/* True only on the owning thread during direct or runner loader teardown.
 * Memory/import operations remain valid; exports and new teardown reject. */
bool qa_native_unloading_owner(const qa_native_instance *instance);
qa_native_backend qa_native_get_backend(const qa_native_instance *instance);
qa_native_lifecycle qa_native_get_lifecycle(const qa_native_instance *instance);
const qa_native_module *qa_native_get_module(const qa_native_instance *instance);
bool qa_native_active(const qa_native_instance *instance);

/* Entry names and signatures are fixed by the selected source API profile.
 * The result may be NULL only for void entries. Calls reject a missing entry,
 * wrong argument type/count, checkpoint activity and a shut-down instance. */
const qa_native_signature *qa_native_entry_signature(const qa_native_instance *instance,
                                                     const char *name);
bool qa_native_call(qa_native_instance *instance, const char *entry,
                    const qa_native_value *arguments, size_t argument_count,
                    qa_native_value *result, qa_error *error);
bool qa_native_initialize(qa_native_instance *instance, qa_error *error);
bool qa_native_shutdown(qa_native_instance *instance, qa_error *error);
/* QL API 10 has a mandatory registration phase and parameterized level
 * initialization/shutdown. These helpers preserve that source lifecycle while
 * qa_native_call remains available for every individual export. */
bool qa_native_ql_register_cvars(qa_native_instance *instance, qa_error *error);
bool qa_native_ql_initialize(qa_native_instance *instance, int32_t level_time, int32_t random_seed,
                             bool restart, qa_error *error);
bool qa_native_ql_shutdown(qa_native_instance *instance, bool restart, qa_error *error);

/* Declared source entry calls use an already validated export or RVA. Inline
 * instruction-region interception requires the separate instrumentation
 * backend and is never approximated by this direct-call operation. */
bool qa_native_export(const qa_native_instance *instance, const char *name, qa_native_address *out,
                      qa_error *error);
/* Reads the admitted profile's actual API callback, including table callbacks
 * which are not image symbol exports. Does not invoke the source entry. */
bool qa_native_entry_address(const qa_native_instance *, const char *, qa_native_address *, qa_error *);
/* Read-only boundary for private restore writes: no active source calls or
 * installed entry, committed-write, or instruction-region observers. */
bool qa_native_restore_ready(const qa_native_instance *, qa_error *);
bool qa_native_rva(const qa_native_instance *instance, uint64_t rva, size_t bytes,
                   qa_native_address *out, qa_error *error);
bool qa_native_invoke(qa_native_instance *instance, qa_native_address entry,
                      const qa_native_signature *signature, const qa_native_value *arguments,
                      size_t argument_count, qa_native_value *result, qa_error *error);

/* Address operations are the only portable way for host services to inspect
 * pointer arguments. Direct backends use checked operating-system process
 * memory operations; runner backends use the same contract remotely. */
bool qa_native_read(const qa_native_instance *instance, qa_native_address source, void *out,
                    size_t bytes, qa_error *error);
bool qa_native_write(qa_native_instance *instance, qa_native_address destination, qa_bytes bytes,
                     qa_error *error);
bool qa_native_read_string(const qa_native_instance *instance, qa_native_address source,
                           size_t maximum, qa_buffer *out, qa_error *error);
bool qa_native_allocate(qa_native_instance *instance, size_t bytes, int32_t tag,
                        qa_native_address *out, qa_error *error);
bool qa_native_free(qa_native_instance *instance, qa_native_address address, qa_error *error);
void qa_native_free_tag(qa_native_instance *instance, int32_t tag);

typedef enum qa_native_slot_kind {
    QA_NATIVE_SLOT_FREE,
    QA_NATIVE_SLOT_WORLD,
    QA_NATIVE_SLOT_OWNED,
    QA_NATIVE_SLOT_BORROWED
} qa_native_slot_kind;

typedef struct qa_native_slot_binding {
    qa_native_slot_kind kind;
    uint32_t slot;
    qa_actor_id actor;
    qa_actor_owner owner;
    uint32_t source_slot;
} qa_native_slot_binding;

typedef struct qa_native_entity_table {
    qa_native_address base;
    size_t stride;
    uint32_t count;
    uint32_t capacity;
} qa_native_entity_table;

/* Q2 export tables publish their entity storage automatically. Q3 hosts call
 * set_entity_table from LOCATE_GAME_DATA; QL API 10 records its table import
 * automatically. Slot bindings refer to the shared actor registry; the ABI
 * byte array is only the module's private shadow. */
bool qa_native_entity_table_get(const qa_native_instance *instance, qa_native_entity_table *out,
                                qa_error *error);
bool qa_native_set_entity_table(qa_native_instance *instance, qa_native_entity_table table,
                                qa_error *error);
bool qa_native_entity_address(const qa_native_instance *instance, uint32_t slot,
                              qa_native_address *out, qa_error *error);
bool qa_native_entity_slot(const qa_native_instance *instance, qa_native_address address,
                           uint32_t *out, qa_error *error);
bool qa_native_bind_slot(qa_native_instance *instance, const qa_native_slot_binding *binding,
                         qa_error *error);
bool qa_native_slot(const qa_native_instance *instance, uint32_t slot, qa_native_slot_binding *out,
                    qa_error *error);

typedef enum qa_native_checkpoint_kind {
    QA_NATIVE_CHECKPOINT_Q2_CLASSIC,
    QA_NATIVE_CHECKPOINT_Q2_RERELEASE,
    QA_NATIVE_CHECKPOINT_HOST_ONLY
} qa_native_checkpoint_kind;

typedef struct qa_native_checkpoint {
    qa_native_checkpoint_kind kind;
    qa_native_profile profile;
    qa_qvm_role q3_role;
    qa_native_image_info image;
    qa_sha256_digest declaration;
    bool has_declaration;
    bool autosave;
    bool transition;
    bool has_game;
    bool has_level;
    bool has_host;
    qa_buffer game;
    qa_buffer level;
    qa_buffer host;
} qa_native_checkpoint;

typedef struct qa_native_checkpoint_request {
    bool game;
    bool level;
    bool autosave;
    bool transition;
} qa_native_checkpoint_request;

typedef enum qa_native_restore_part {
    QA_NATIVE_RESTORE_GAME,
    QA_NATIVE_RESTORE_LEVEL,
    QA_NATIVE_RESTORE_HOST
} qa_native_restore_part;

/* Capture/restore require an idle initialized instance. Classic modules use
 * their original Write/ReadGame and Write/ReadLevel files; rerelease modules
 * use their JSON exports. Game and level restore remain separate because map
 * spawning belongs between those source operations. The host part must cover
 * slot-to-actor bindings, source ownership and any shared service continuation
 * that the module cannot serialize itself. Q3/QL bindings must cover their
 * complete guest-visible continuation because those APIs expose no save ABI. */
bool qa_native_checkpoint_capture(qa_native_instance *instance,
                                  qa_native_checkpoint_request request, qa_native_checkpoint *out,
                                  qa_error *error);
bool qa_native_checkpoint_restore(qa_native_instance *instance,
                                  const qa_native_checkpoint *checkpoint,
                                  qa_native_restore_part part, qa_error *error);
void qa_native_checkpoint_free(qa_native_checkpoint *checkpoint);
bool qa_native_checkpoint_encode(const qa_native_checkpoint *checkpoint, qa_buffer *out,
                                 qa_error *error);
bool qa_native_checkpoint_decode(qa_bytes encoded, qa_native_checkpoint *out, qa_error *error);

/* The caller acquires a declaration from the artifact's own content directory.
 * Loading validates version, normalized artifact path, exact digest/API and
 * all required primary sections. The typed composition layer validates each
 * section's complete field/region schema before Init. */
bool qa_native_declaration_load(qa_bytes json, const char *artifact_path,
                                const qa_native_module *module, qa_native_declaration **out,
                                qa_error *error);
void qa_native_declaration_destroy(qa_native_declaration *declaration);
const qa_sha256_digest *qa_native_declaration_digest(const qa_native_declaration *declaration);
qa_bytes qa_native_declaration_primary(const qa_native_declaration *declaration);

typedef struct qa_native_declared_region {
    uint32_t id;
    const char *path; /* Borrowed until declaration or instance destruction. */
    uint32_t entry_rva;
    uint32_t join_rva;
    bool has_frame;
    uint32_t frame_entry_rva;
    uint32_t frame_exit_rva;
} qa_native_declared_region;

size_t qa_native_declaration_region_count(const qa_native_declaration *declaration);
bool qa_native_declaration_region(const qa_native_declaration *declaration, size_t index,
                                  qa_native_declared_region *out, qa_error *error);
bool qa_native_declaration_find_region(const qa_native_declaration *declaration, const char *path,
                                       qa_native_declared_region *out, qa_error *error);

/* JSON-pointer queries are relative to primary and keep the compatibility
 * profile's non-region fields available to typed composition consumers without
 * duplicating the document. Source views borrow declaration storage; decoded
 * strings are owned and released with qa_buffer_free. */
qa_json_kind qa_native_declaration_kind(const qa_native_declaration *declaration,
                                        const char *json_pointer);
bool qa_native_declaration_u64(const qa_native_declaration *declaration, const char *json_pointer,
                               uint64_t *out, qa_error *error);
bool qa_native_declaration_number(const qa_native_declaration *declaration,
                                  const char *json_pointer, double *out, qa_error *error);
bool qa_native_declaration_bool(const qa_native_declaration *declaration, const char *json_pointer,
                                bool *out, qa_error *error);
bool qa_native_declaration_string(const qa_native_declaration *declaration,
                                  const char *json_pointer, qa_buffer *out, qa_error *error);
bool qa_native_declaration_source(const qa_native_declaration *declaration,
                                  const char *json_pointer, qa_bytes *out, qa_error *error);

typedef enum qa_native_region_phase {
    QA_NATIVE_REGION_ENTER,
    QA_NATIVE_REGION_JOIN
} qa_native_region_phase;

typedef enum qa_native_register {
    QA_NATIVE_RAX,
    QA_NATIVE_RCX,
    QA_NATIVE_RDX,
    QA_NATIVE_RBX,
    QA_NATIVE_RSP,
    QA_NATIVE_RBP,
    QA_NATIVE_RSI,
    QA_NATIVE_RDI,
    QA_NATIVE_R8,
    QA_NATIVE_R9,
    QA_NATIVE_R10,
    QA_NATIVE_R11,
    QA_NATIVE_R12,
    QA_NATIVE_R13,
    QA_NATIVE_R14,
    QA_NATIVE_R15,
    QA_NATIVE_REGISTER_COUNT
} qa_native_register;

typedef struct qa_native_processor_state {
    uint64_t registers[QA_NATIVE_REGISTER_COUNT];
    uint8_t simd[16][16]; /* XMM0-7 on i386; XMM0-15 on x86-64. */
    uint64_t flags;
    qa_native_address instruction; /* Observed boundary; replacement is ignored. */
} qa_native_processor_state;

typedef struct qa_native_region_event {
    qa_native_declared_region region;
    qa_native_region_phase phase;
    qa_native_processor_state state;
} qa_native_region_event;

typedef enum qa_native_region_action {
    QA_NATIVE_REGION_CONTINUE,
    QA_NATIVE_REGION_SKIP_TO_JOIN,
    QA_NATIVE_REGION_RETURN_TO_FRAME_EXIT,
    QA_NATIVE_REGION_FAIL_INSTANCE
} qa_native_region_action;

typedef struct qa_native_region_decision {
    qa_native_region_action action;
    bool replace_state;
    qa_native_processor_state state;
} qa_native_region_decision;

/* Region callbacks run synchronously on the owning call stack. Bind before
 * initialization. Multiple bindings observe registration order; state changes
 * feed the next observer and terminal actions must agree. The callback may use
 * guest-memory reads/writes and other providers, but cannot reenter its
 * suspended instance. RETURN is admitted only for a declaration carrying an
 * explicit frame exit. Binding changes requested while the instance is active
 * are rejected; the void unbind operation leaves the binding installed. */
typedef bool (*qa_native_region_fn)(void *context, qa_native_instance *instance,
                                    const qa_native_region_event *event,
                                    qa_native_region_decision *decision, qa_error *error);
size_t qa_native_region_count(const qa_native_instance *instance);
bool qa_native_region(const qa_native_instance *instance, size_t index,
                      qa_native_declared_region *out, qa_error *error);
bool qa_native_bind_region(qa_native_instance *instance, uint32_t region_id,
                           qa_native_region_fn callback, void *context,
                           qa_native_region_binding **out, qa_error *error);
void qa_native_unbind_region(qa_native_region_binding *binding);
/* Successful removal consumes the binding. An active/failed removal retains
 * it and its callback context so its actual owner can retry at a safe point. */
bool qa_native_remove_region(qa_native_region_binding *binding, qa_error *error);

/* Entry point for the target-built helper executable. Its stdin/stdout are the
 * framed binary transport and must not be used for logging. */
int qa_native_runner_main(void);

#endif
