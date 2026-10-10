#ifndef QA_NATIVE_H
#define QA_NATIVE_H

#include "qa/actors.h"
#include "qa/json.h"
#include "qa/qvm.h"

typedef struct qa_native_module qa_native_module;
typedef struct qa_native_instance qa_native_instance;
typedef struct qa_native_declaration qa_native_declaration;
typedef struct qa_native_region_binding qa_native_region_binding;
struct qa_native_process_options;

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
} qa_native_image_info;

/* Inspection never executes the artifact. PE and ELF structural metadata is
 * validated before a module is admitted to the owned guest process.
 */
bool qa_native_inspect(qa_bytes image, qa_native_image_info *out, qa_error *error);
/* Packaging inspection for an executable, including ELF PIE. Module admission
 * continues to require a DLL/shared object through qa_native_inspect. */
bool qa_native_inspect_program(qa_bytes image, qa_native_image_info *out, qa_error *error);
qa_native_target qa_native_host_target(void);

typedef struct qa_native_module_info {
    qa_native_profile profile;
    qa_native_image_info image;
    const char *source; /* Borrowed until the module is released. */
} qa_native_module_info;

/* Modules own immutable artifact bytes. Instances retain the module and load a
 * distinct image so file-scope guest globals are never shared accidentally. */
bool qa_native_module_load(qa_bytes image, const char *source, qa_native_profile profile,
                           qa_native_module **out, qa_error *error);
/* Immutable image metadata must identify one writable, non-executable source
 * section/load segment; ELF RELRO is excluded. No source code executes. */
bool qa_native_module_mutable_range(const qa_native_module *, uint64_t rva, uint64_t bytes, qa_error *);
void qa_native_module_retain(qa_native_module *module);
void qa_native_module_release(qa_native_module *module);
qa_native_module_info qa_native_module_describe(const qa_native_module *module);
qa_bytes qa_native_module_bytes(const qa_native_module *module);

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
typedef enum qa_native_entity_change {
    QA_NATIVE_ENTITIES_TABLE,
    QA_NATIVE_ENTITIES_SLOT,
    QA_NATIVE_ENTITIES_INVALIDATE
} qa_native_entity_change;
typedef struct qa_native_entity_event {
    qa_native_entity_change change;
    uint32_t slot;
    qa_native_address address;
    size_t bytes;
} qa_native_entity_event;
/* Cold admission/release notifications. TABLE/SLOT follow actual commits;
 * INVALIDATE precedes restore, unload and storage retirement. Non-slot events
 * use UINT32_MAX. INVALIDATE address zero means the whole instance; otherwise
 * only the stated memory span retires. The callback owns no source storage. */
typedef void (*qa_native_entity_observer_fn)(void *context, qa_native_instance *,
    const qa_native_entity_event *);

/* Context and callback functions remain valid until instance destruction.
 * Declaration ownership is retained and region records are copied. */
typedef struct qa_native_options {
    void *context;
    qa_qvm_role q3_role;
    qa_native_import_fn import;
    qa_native_syscall_describe_fn describe_syscall;
    qa_native_syscall_fn syscall;
    qa_native_host_checkpoint_fn checkpoint;
    qa_native_host_restore_fn restore;
    const qa_native_declaration *declaration;
    uint32_t tick_rate;
    float frame_seconds;
    uint32_t frame_milliseconds;
    bool observe;
    const struct qa_native_process_options *process;
    qa_native_entity_observer_fn entity_changed;
} qa_native_options;

typedef enum qa_native_backend {
    QA_NATIVE_BACKEND_NONE,
    QA_NATIVE_BACKEND_OWNED_PROCESS
} qa_native_backend;

/* Packaging inputs for the owned hardware child and its optional monitor. */
typedef bool (*qa_native_profile_validate_fn)(void *context, qa_error *error);
typedef struct qa_native_runtime_config {
    const char *bootstrap;
    const char *linux_x86_64_drrun;
    const char *linux_x86_64_profile;
    qa_native_profile_validate_fn validate_profile;
    void *validation_context;
} qa_native_runtime_config;

typedef enum qa_native_lifecycle {
    QA_NATIVE_LOADED,
    QA_NATIVE_PREINITIALIZED,
    QA_NATIVE_INITIALIZED,
    QA_NATIVE_SHUT_DOWN,
    QA_NATIVE_RESTART_READY
} qa_native_lifecycle;

/* Creation consumes the supplied process graph through options.process.
 * Imports are synchronous and permitted nested calls restore the active owner.
 * Failed construction retains any partially live owner in *out for cleanup. */
bool qa_native_create(qa_native_module *module, const qa_native_options *options,
                      qa_native_instance **out, qa_error *error);
/* Clears the owner after actual consumption, including source callback and
 * guest execution faults. A rejected unload or retained original mapping keeps
 * every callback context in *owner. Cleanup never releases a closed loader
 * reference twice; a later attempt qualifies actual image retirement. */
bool qa_native_destroy_owned(qa_native_instance **owner, qa_error *error);
/* True only when destruction will pass its initial ownership admission.
 * Shutdown or guest cleanup may still fail after admission. */
bool qa_native_can_destroy(const qa_native_instance *instance);
/* Readonly Q3 GAME/QL round admission. Raw-address observers must retire;
 * immutable declaration regions retain their original RVA identities. */
bool qa_native_restart_ready(const qa_native_instance *, qa_error *);
/* After genuine Shutdown(restart=true) and complete actor-slot retirement,
 * reload the owned original artifact and dependencies beneath this instance.
 * Refresh imports and exports before admitting Init(restart=true). A source or
 * loader fault consumes the reset attempt and prevents initialization. */
bool qa_native_restart_original(qa_native_instance *, qa_error *);
/* A terminal owned guest cannot execute further source operations. This
 * does not authorize replacement of a live source primary or lost snapshot. */
bool qa_native_terminal(const qa_native_instance *instance);
/* Read-only parent slot ownership check after actual canonical retirement.
 * Cached bindings are used only for this terminal lifetime qualification. */
bool qa_native_terminal_retired(const qa_native_instance *, const qa_actor_registry *);
/* True only on the owning thread during owned module teardown.
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

/* Declared source calls and instruction observers use the same owned guest. */
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
/* Set only at the actual admitted source dispatch boundary. The receipt stays
 * true if entered source execution subsequently fails; ABI/preflight refusal
 * leaves it false. The output is required and is cleared before admission. */
bool qa_native_invoke_receipt(qa_native_instance *, qa_native_address,
    const qa_native_signature *, const qa_native_value *, size_t,
    qa_native_value *, bool *entered, qa_error *);

/* Address operations read and write the owned guest mappings. */
bool qa_native_read(const qa_native_instance *instance, qa_native_address source, void *out,
                    size_t bytes, qa_error *error);
bool qa_native_write(qa_native_instance *instance, qa_native_address destination, qa_bytes bytes,
                     qa_error *error);
enum {
    QA_NATIVE_MEMORY_READ = 1,
    QA_NATIVE_MEMORY_WRITE = 2,
    QA_NATIVE_MEMORY_EXECUTE = 4
};
/* Pure mapping, permission and backing/EOF observation without trial writes. */
bool qa_native_range_check(const qa_native_instance *, qa_native_address,
    size_t, uint32_t permissions, qa_error *);
/* Cold read-only view of live module storage from the controller backing,
 * never a cast of a guest address. A span crossing mappings must remain
 * contiguous within one backing.
 * The caller retires views before their allocation is freed, unmap/protection changes,
 * backing or entity-table replacement, restore, and instance destruction.
 * Mapping/table metadata growth alone does not relocate the backing bytes.
 * Resolve while execution is stopped; loads through the returned bytes do
 * not validate, copy, synchronize execution, or pin the underlying storage. */
bool qa_native_borrow(const qa_native_instance *, qa_native_address,
    size_t, qa_bytes *, qa_error *);
/* Validates a terminated string through current readable mappings. size excludes
 * the NUL; data borrows contiguous backing under qa_native_borrow's lifetime.
 * A valid string crossing distinct backing returns data=NULL and its length,
 * so callers that need custody can use qa_native_read_string without rejecting it. */
bool qa_native_string_span(const qa_native_instance *, qa_native_address,
    size_t maximum, qa_bytes *, qa_error *);
bool qa_native_read_string(const qa_native_instance *instance, qa_native_address source,
                           size_t maximum, qa_buffer *out, qa_error *error);
bool qa_native_allocate(qa_native_instance *instance, size_t bytes, int32_t tag,
                        qa_native_address *out, qa_error *error);
typedef struct qa_native_allocation_info {
    qa_native_address base;
    uint64_t bytes;
    int32_t tag;
} qa_native_allocation_info;
/* Only original host-tagged allocations qualify; arbitrary process memory is
 * not treated as an owned source object. Interior addresses retain their base. */
bool qa_native_allocation_query(const qa_native_instance *, qa_native_address,
                                qa_native_allocation_info *, qa_error *);
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
typedef struct qa_native_entity_projection_view {
    qa_native_entity_table table;
    qa_bytes slots;
    const uint32_t *count;
    uint32_t slot_stride, actor_offset, kind_offset;
} qa_native_entity_projection_view;
/* Existing native_slot rows and current count, borrowed without a copy. Slot
 * kind is qa_native_slot_kind; actor bytes contain qa_actor_id. Re-acquire on
 * TABLE and retire on INVALIDATE. SLOT changes are already visible in place. */
bool qa_native_entity_projection(const qa_native_instance *,
    qa_native_entity_projection_view *, qa_error *);

/* Q2 export tables publish their entity storage automatically. Q3 hosts call
 * set_entity_table from LOCATE_GAME_DATA; QL API 10 records its table import
 * automatically. Slot bindings refer to the shared actor registry; the ABI
 * byte array is only the module's private shadow. */
bool qa_native_entity_table_get(const qa_native_instance *instance, qa_native_entity_table *out,
                                qa_error *error);
/* Reads the actual original export table without invoking a source entry. */
bool qa_native_entity_table_refresh(qa_native_instance *, qa_native_entity_table *, qa_error *);
/* Last parent-owned table metadata only, after an owned guest has become
 * terminal and all source callbacks have drained. Addresses cannot be used
 * for source memory operations; this supports actual actor-release cleanup. */
bool qa_native_terminal_entity_table(const qa_native_instance *, qa_native_entity_table *,
                                     qa_error *);
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
    QA_NATIVE_CHECKPOINT_HOST_ONLY,
    QA_NATIVE_CHECKPOINT_OWNED_PROCESS
} qa_native_checkpoint_kind;

typedef struct qa_native_checkpoint {
    qa_native_checkpoint_kind kind;
    qa_native_profile profile;
    qa_qvm_role q3_role;
    qa_native_image_info image;
    qa_buffer source;
    bool has_declaration;
    bool autosave;
    bool transition;
    bool has_game;
    bool has_level;
    bool has_host;
    bool has_process;
    qa_buffer game;
    qa_buffer level;
    qa_buffer host;
    qa_buffer process;
} qa_native_checkpoint;

typedef struct qa_native_checkpoint_request {
    bool game;
    bool level;
    bool host;
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
 * complete guest-visible continuation because those APIs expose no save ABI.
 * HOST capture is requested separately from the source files. Q2 always uses
 * its original GAME/LEVEL files and reconstructs a fresh module on restore.
 * Other owned processes capture their private CPU/RAM/runtime capsule. With
 * game and level both false, no source exporter runs. */
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
 * Loading validates version, normalized artifact path, API and
 * all required primary sections. The typed composition layer validates each
 * section's complete field/region schema before Init. */
bool qa_native_declaration_load(qa_bytes json, const char *artifact_path,
                                const qa_native_module *module, qa_native_declaration **out,
                                qa_error *error);
void qa_native_declaration_destroy(qa_native_declaration *declaration);
qa_bytes qa_native_declaration_primary(const qa_native_declaration *declaration);
/* The original callback document is a distinct contract. A primary wrapper
 * has no callback document, and a callback document has no primary wrapper. */
qa_bytes qa_native_declaration_callbacks(const qa_native_declaration *declaration);

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
 * guest-memory reads/writes and other providers. Same-instance source calls
 * require qa_native_region_invoke with this exact borrowed event; ordinary
 * invocation stays fenced. RETURN is admitted only for a declaration carrying an
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
/* Invoke the caller's declared whole-function entry while this exact region
 * callback is suspended. The entry must match an actual declared region entry
 * in the same loaded image and use its fixed ABI; it need not be this scan's
 * entry. Nested calls retain the enclosing processor state;
 * source memory effects remain committed. The event expires on callback return.
 * This does not bypass region observers: callers implement their source-defined
 * scanner bypass and restoration around the original invocation. */
bool qa_native_region_invoke(qa_native_instance *instance,
                             const qa_native_region_event *event, qa_native_address entry,
                             const qa_native_signature *signature,
                             const qa_native_value *arguments, size_t argument_count,
                             qa_native_value *result, qa_error *error);

#endif
