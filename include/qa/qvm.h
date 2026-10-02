#ifndef QA_QVM_H
#define QA_QVM_H

#include "qa/common.h"
#include "qa/hash.h"
#include "qa/math.h"

typedef enum qa_qvm_opcode {
    QA_QVM_UNDEF, QA_QVM_IGNORE, QA_QVM_BREAK, QA_QVM_ENTER, QA_QVM_LEAVE,
    QA_QVM_CALL, QA_QVM_PUSH, QA_QVM_POP, QA_QVM_CONST, QA_QVM_LOCAL, QA_QVM_JUMP,
    QA_QVM_EQ, QA_QVM_NE, QA_QVM_LTI, QA_QVM_LEI, QA_QVM_GTI, QA_QVM_GEI,
    QA_QVM_LTU, QA_QVM_LEU, QA_QVM_GTU, QA_QVM_GEU,
    QA_QVM_EQF, QA_QVM_NEF, QA_QVM_LTF, QA_QVM_LEF, QA_QVM_GTF, QA_QVM_GEF,
    QA_QVM_LOAD1, QA_QVM_LOAD2, QA_QVM_LOAD4, QA_QVM_STORE1, QA_QVM_STORE2, QA_QVM_STORE4,
    QA_QVM_ARG, QA_QVM_BLOCK_COPY, QA_QVM_SEX8, QA_QVM_SEX16,
    QA_QVM_NEGI, QA_QVM_ADD, QA_QVM_SUB, QA_QVM_DIVI, QA_QVM_DIVU, QA_QVM_MODI, QA_QVM_MODU,
    QA_QVM_MULI, QA_QVM_MULU, QA_QVM_BAND, QA_QVM_BOR, QA_QVM_BXOR, QA_QVM_BCOM,
    QA_QVM_LSH, QA_QVM_RSHI, QA_QVM_RSHU, QA_QVM_NEGF, QA_QVM_ADDF, QA_QVM_SUBF,
    QA_QVM_DIVF, QA_QVM_MULF, QA_QVM_CVIF, QA_QVM_CVFI
} qa_qvm_opcode;
typedef enum qa_qvm_role { QA_QVM_GAME, QA_QVM_CGAME, QA_QVM_UI } qa_qvm_role;
typedef enum qa_qvm_abi { QA_QVM_Q3_MODERN, QA_QVM_Q3_116N } qa_qvm_abi;
typedef enum qa_qvm_semantics { QA_QVM_INTERPRETED, QA_QVM_COMPILED_SEMANTICS } qa_qvm_semantics;
typedef struct qa_qvm_instruction { uint32_t byte_offset; int32_t operand; uint8_t opcode, operand_width; } qa_qvm_instruction;
typedef struct qa_qvm_image qa_qvm_image;
typedef struct qa_qvm qa_qvm;
typedef struct qa_vfs qa_vfs;

/* Decoded code and initialized bytes are owned, immutable and refcounted.
 * Instances share the image and allocate private RAM. References and instances
 * have one thread owner or require external synchronization. */
bool qa_qvm_image_load(qa_bytes bytes, qa_qvm_image **out, qa_error *error);
void qa_qvm_image_retain(qa_qvm_image *image);
void qa_qvm_image_release(qa_qvm_image *image);
const qa_sha256_digest *qa_qvm_image_digest(const qa_qvm_image *image);
const qa_qvm_instruction *qa_qvm_image_instructions(const qa_qvm_image *image, size_t *count);
/* Borrowed original data/literal bytes; excludes BSS and allocation padding. */
qa_bytes qa_qvm_image_initialized_data(const qa_qvm_image *image);
size_t qa_qvm_image_memory_size(const qa_qvm_image *image);
/* A source global word must fit data/literal/BSS, excluding allocation padding
 * and the source stack. This only qualifies immutable artifact ownership. */
bool qa_qvm_qualify_global_word(const qa_qvm_image *, uint32_t offset, qa_error *);
/* Byte ranges retain the original data/literal/BSS extent, including a final
 * partial word, without admitting allocation padding. */
bool qa_qvm_qualify_source_span(const qa_qvm_image *, uint32_t offset, size_t length, qa_error *);

typedef struct qa_qvm_compatibility {
    qa_qvm_abi abi;
    bool declared;
    qa_buffer primary, equipment_presentation; /* Owned JSON values, if declared. */
} qa_qvm_compatibility;
bool qa_qvm_compatibility_parse(qa_bytes json, const char *artifact_path,
                                 const qa_sha256_digest *, qa_qvm_role,
                                 qa_qvm_compatibility *, qa_error *);
bool qa_qvm_compatibility_read(qa_vfs *, const char *artifact_path,
                                const qa_sha256_digest *, qa_qvm_role,
                                qa_qvm_compatibility *, qa_error *);
void qa_qvm_compatibility_free(qa_qvm_compatibility *);
bool qa_qvm_image_open(qa_vfs *, const char *artifact_path, qa_qvm_role,
                         qa_qvm_image **, qa_qvm_compatibility *, qa_error *);

/* A call token is valid only for the callback that issued it. Guest arguments
 * stay in original memory so aliased writes and nested calls see committed data. */
typedef struct qa_qvm_call {
    qa_qvm *vm;
    uint64_t token;
    uint32_t instruction, caller_instruction;
    uint32_t argument_base;
    size_t argument_count;
} qa_qvm_call;
typedef bool (*qa_qvm_syscall_fn)(void *context, const qa_qvm_call *call, int32_t trap,
                                 int32_t *result, qa_error *error);
typedef bool (*qa_qvm_host_checkpoint_fn)(void *context, qa_buffer *state, qa_error *error);
typedef bool (*qa_qvm_host_restore_fn)(void *context, qa_bytes state, qa_error *error);
typedef struct qa_qvm_trace_event {
    uint32_t instruction; /* UINT32_MAX when returning to a noninstruction byte slot. */
    uint32_t byte_offset, program_stack;
    size_t operand_count;
    uint8_t opcode;
} qa_qvm_trace_event;
/* Diagnostic callbacks may inspect state, but cannot mutate or reenter it. */
typedef bool (*qa_qvm_trace_fn)(void *context, const qa_qvm *, const qa_qvm_trace_event *, qa_error *);
typedef struct qa_qvm_options {
    qa_qvm_role role;
    qa_qvm_abi abi;
    qa_qvm_semantics semantics;
    qa_qvm_syscall_fn syscall;
    void *context;
    qa_qvm_host_checkpoint_fn checkpoint;
    qa_qvm_host_restore_fn restore;
    uint64_t instruction_limit; /* Zero means no artificial execution limit. */
    bool debug;
    qa_qvm_trace_fn trace, breakpoint;
} qa_qvm_options;

bool qa_qvm_create(qa_qvm_image *, const qa_qvm_options *, qa_qvm **out, qa_error *);
bool qa_qvm_destroy(qa_qvm *, qa_error *);
/* Includes source execution, write delivery, publication and lifecycle callbacks. */
bool qa_qvm_can_destroy(const qa_qvm *);
bool qa_qvm_invoke(qa_qvm *, uint32_t instruction, const int32_t *words, size_t count, int32_t *result, qa_error *);
/* Set started only when the admitted source entry or its bound implementation
 * begins, after argument-frame setup. It remains set on a source failure. */
bool qa_qvm_invoke_started(qa_qvm *, uint32_t instruction, const int32_t *words,
    size_t count, int32_t *result, bool *started, qa_error *);
bool qa_qvm_restart(qa_qvm *, qa_bytes replacement_image, qa_error *);
/* Same-artifact map restart retains the admitted image/executor and resets RAM
 * from that image's immutable initialized data without reopening its source. */
bool qa_qvm_restart_original(qa_qvm *, qa_error *);
bool qa_qvm_active(const qa_qvm *);
uint32_t qa_qvm_break_count(const qa_qvm *);
qa_qvm_role qa_qvm_get_role(const qa_qvm *);
qa_qvm_abi qa_qvm_get_abi(const qa_qvm *);
uint32_t qa_qvm_api_version(const qa_qvm *);
/* Classify a source trap without executing it. Legacy services map to modern
 * engine numbers; recognized extensions retain their source number. */
bool qa_qvm_classify_syscall(qa_qvm_role, qa_qvm_abi, int32_t trap,
                             int32_t *canonical, bool *engine, qa_error *);
size_t qa_qvm_memory_size(const qa_qvm *);
const qa_sha256_digest *qa_qvm_digest(const qa_qvm *);
bool qa_qvm_call_argument(const qa_qvm_call *, size_t index, int32_t *, qa_error *);
bool qa_qvm_call_set_argument(const qa_qvm_call *, size_t index, int32_t, qa_error *);

/* Original callback words use OP_CALL's signed convention: nonnegative source
 * function entries, or -1-trap for imported services. The exact admitted image
 * and current callback token are required. Imported calls use the ordinary
 * role/ABI dispatcher and a real nested argument frame. */
bool qa_qvm_invoke_source_callback(const qa_qvm_call *, const qa_qvm_image *,
    int32_t pointer, const int32_t *words, size_t count, int32_t *, qa_error *);
/* Pure capability qualification from immutable data/literal/BSS extents.
 * Scratch begins at their aligned end and stays below the 64 KiB source stack
 * reservation. This does not grant a mutable memory lease. */
bool qa_qvm_source_scratch_qualify(const qa_qvm_image *, size_t length,
    uint32_t *offset, qa_error *);
typedef bool (*qa_qvm_source_scratch_fn)(void *, const qa_qvm_call *,
    uint32_t offset, qa_error *);
/* The same current source token remains valid in perform. A real execution
 * stack floor protects scratch from all nested source calls. Save/restore is
 * scoped, including nested uses of the same span and failed source calls.
 * Restore commits the original RAM even if write delivery allocation fails;
 * that failure is reported and never presented as successful delivery. */
bool qa_qvm_source_scratch(const qa_qvm_call *, const qa_qvm_image *, size_t length,
    qa_qvm_source_scratch_fn perform, void *, qa_error *);
typedef struct qa_qvm_source_frame { uint32_t start, end; } qa_qvm_source_frame;
/* The current intercepted function has not entered its original OP_ENTER yet.
 * Returns that original local frame from its real caller stack, without masking.
 * Requires the exact image and current function token before proceeding. */
bool qa_qvm_call_source_frame(const qa_qvm_call *, const qa_qvm_image *,
    qa_qvm_source_frame *, qa_error *);
typedef bool (*qa_qvm_source_word_fn)(void *, const qa_qvm_call *, qa_error *);
/* Project one qualified global word for this intercepted function and restore
 * its exact prior bytes after perform, including cancellation/source failure.
 * Restoration commits RAM even if write delivery cannot allocate. */
bool qa_qvm_source_global_word(const qa_qvm_call *, const qa_qvm_image *,
    uint32_t offset, int32_t value, qa_qvm_source_word_fn, void *, qa_error *);

typedef struct qa_qvm_source_word { uint32_t offset; int32_t value; } qa_qvm_source_word;
typedef struct qa_qvm_word_projection qa_qvm_word_projection;
/* The actual record owner qualifies each dynamic address before borrowing it.
 * Begin copies every original word before the ordered temporary RAM writes.
 * These substitutions and their restoration do not publish committed source
 * writes. The source instructions executed within the scope retain their
 * normal observers. End consumes the real lease, including source failures.
 * restore=false is for an owner
 * whose actor/record was retired or replaced while the source call ran. */
bool qa_qvm_source_words_begin(qa_qvm *, const qa_qvm_image *,
    const qa_qvm_source_word *, size_t, qa_qvm_word_projection **, qa_error *);
/* Capture-only lease, for source-owned results produced by the ensuing call.
 * No projected write or observer delivery occurs at capture. */
bool qa_qvm_source_words_capture(qa_qvm *, const qa_qvm_image *,
    const uint32_t *, size_t, qa_qvm_word_projection **, qa_error *);
/* Observer-aware Source DataView writes use these variants. The lease retains
 * that policy for ordered restoration, including failed Source calls. */
bool qa_qvm_source_words_begin_observed(qa_qvm *, const qa_qvm_image *,
    const qa_qvm_source_word *, size_t, qa_qvm_word_projection **, qa_error *);
bool qa_qvm_source_words_capture_observed(qa_qvm *, const qa_qvm_image *,
    const uint32_t *, size_t, qa_qvm_word_projection **, qa_error *);
bool qa_qvm_source_words_end(qa_qvm_word_projection **, bool restore, qa_error *);
/* The retained lease is the executor's actual last open word projection.
 * Returned owners use this relation to unwind nested cleanup in source order. */
bool qa_qvm_source_words_is_last(const qa_qvm_word_projection *);
/* No Source execution, host callback, scratch, write delivery or lifecycle
 * callback is entered. Retained word projections may still require cleanup. */
bool qa_qvm_source_returned(const qa_qvm *);
/* An actual raw Source record owner qualifies its full actor and byte range
 * before copying private RAM. This counted copy bypasses committed
 * observers; Source instructions and qa_qvm_write retain normal publication. */
bool qa_qvm_source_bytes_write(qa_qvm *, const qa_qvm_image *, uint32_t,
    qa_bytes, qa_error *);
typedef bool (*qa_qvm_source_scratch_run_fn)(void *, qa_qvm *, uint32_t, qa_error *);
/* A real source scratch lease also supports host-initiated source actions.
 * Nested calls inherit its stack floor and exact bytes restore on all exits. */
bool qa_qvm_source_scratch_run(qa_qvm *, const qa_qvm_image *, size_t,
    qa_qvm_source_scratch_run_fn, void *, qa_error *);
/* Generic Mod arguments retain their four-byte bump cursor and reserve space
 * below the current paused stack. The caller supplies its donor reservation. */
bool qa_qvm_source_scratch_run_reserved(qa_qvm *, const qa_qvm_image *, size_t,
    uint32_t stack_reservation, qa_qvm_source_scratch_run_fn, void *, qa_error *);

/* Raw offsets are checked without masking. Host pointer APIs mask only the
 * base word and treat zero as NULL. Mutable host writes use these operations. */
bool qa_qvm_read(const qa_qvm *, uint32_t offset, void *out, size_t length, qa_error *);
bool qa_qvm_write(qa_qvm *, uint32_t offset, qa_bytes bytes, qa_error *);
bool qa_qvm_fill(qa_qvm *, uint32_t offset, size_t length, uint8_t value, qa_error *);
bool qa_qvm_copy(qa_qvm *, uint32_t destination, uint32_t source, size_t length, qa_error *);
bool qa_qvm_span(const qa_qvm *, int32_t pointer, int64_t relative, size_t length, qa_bytes *, qa_error *);
bool qa_qvm_read_string(const qa_qvm *, int32_t pointer, qa_bytes *, qa_error *);
bool qa_qvm_write_string(qa_qvm *, int32_t pointer, qa_bytes string, size_t capacity, qa_error *);
uint32_t qa_qvm_mask_address(const qa_qvm *, int32_t address);

typedef uint64_t qa_qvm_binding;
typedef bool (*qa_qvm_function_hook)(void *context, const qa_qvm_call *, int32_t *result, qa_error *);
typedef bool (*qa_qvm_function_observer)(void *context, const qa_qvm_call *, qa_error *);
typedef qa_qvm_function_hook (*qa_qvm_function_resolver)(void *context, const qa_qvm_call *, void **hook_context);
bool qa_qvm_bind_function(qa_qvm *, uint32_t instruction, bool include_host_invocations,
                          qa_qvm_function_hook, void *, qa_qvm_binding *, qa_error *);
bool qa_qvm_observe_function(qa_qvm *, uint32_t instruction, qa_qvm_function_observer,
                             void *, qa_qvm_binding *, qa_error *);
bool qa_qvm_bind_resolver(qa_qvm *, qa_qvm_function_resolver, void *, qa_qvm_binding *, qa_error *);
bool qa_qvm_unbind(qa_qvm *, qa_qvm_binding, qa_error *);
bool qa_qvm_proceed(const qa_qvm_call *, int32_t *result, qa_error *);
bool qa_qvm_cancel(const qa_qvm_call *, qa_error *);
/* Observe an outstanding source cancellation from the current callback token.
 * The observation does not consume cancellation or change its ancestor scope. */
bool qa_qvm_call_cancelled(const qa_qvm_call *, bool *, qa_error *);
bool qa_qvm_local_word(const qa_qvm_call *, uint32_t offset, int32_t *, qa_error *);

typedef bool (*qa_qvm_branch_fn)(void *, const qa_qvm_call *, bool original, bool *taken, qa_error *);
typedef struct qa_qvm_branch_binding { uint32_t instruction; qa_qvm_branch_fn decide; void *context; } qa_qvm_branch_binding;
typedef bool (*qa_qvm_region_fn)(void *, const qa_qvm_call *, bool *skip, qa_error *);
typedef bool (*qa_qvm_region_done_fn)(void *, const qa_qvm_call *, qa_error *);
typedef struct qa_qvm_region_binding {
    uint32_t entry, join;
    qa_qvm_region_fn enter;
    qa_qvm_region_done_fn completed;
    void *context;
} qa_qvm_region_binding;
typedef struct qa_qvm_region_evaluation {
    uint32_t entry, join;
    const uint32_t *inputs;
    size_t input_count;
    int32_t result_offset; /* -1 means no local result. */
    bool read_only;
} qa_qvm_region_evaluation;
typedef struct qa_qvm_evaluation_stack { uint32_t floor, top; } qa_qvm_evaluation_stack;
/* Immutable declaration qualification; the actual evaluator additionally
 * admits this reservation against its live caller stack and inherited floor. */
bool qa_qvm_qualify_evaluation_stack(const qa_qvm_image *, const qa_qvm_evaluation_stack *, qa_error *);
bool qa_qvm_bind_branches(const qa_qvm_call *, const qa_qvm_branch_binding *, size_t, qa_error *);
bool qa_qvm_bind_regions(const qa_qvm_call *, const qa_qvm_region_binding *, size_t, qa_error *);
bool qa_qvm_qualify_region(const qa_qvm_image *, uint32_t owner, const qa_qvm_region_evaluation *, qa_error *);
bool qa_qvm_qualify_source_region(const qa_qvm_image *, uint32_t owner,
                                  uint32_t entry, uint32_t join, qa_error *);
bool qa_qvm_evaluate_region(qa_qvm *, uint32_t owner, const int32_t *arguments, size_t argument_count,
                            const qa_qvm_region_evaluation *, const int32_t *inputs,
                            const qa_qvm_evaluation_stack *, int32_t *, qa_error *);
/* Consumes the intercepted function's one proceed opportunity, retaining its
 * original caller frame and live argument aliases. */
bool qa_qvm_evaluate_call_region(const qa_qvm_call *, const qa_qvm_region_evaluation *,
                                 const int32_t *inputs, int32_t *out, qa_error *);
bool qa_qvm_evaluate_counter(qa_qvm *, uint32_t address, int32_t initial,
                             const uint32_t *functions, size_t function_count,
                             uint32_t instruction, const int32_t *arguments, size_t argument_count,
                             const qa_qvm_evaluation_stack *, int32_t *out, qa_error *);

typedef struct qa_qvm_write_range { uint32_t offset; size_t length; } qa_qvm_write_range;
typedef struct qa_qvm_committed_range { uint32_t offset; qa_bytes before, after; } qa_qvm_committed_range;
typedef struct qa_qvm_committed_write { uint64_t sequence; const qa_qvm_committed_range *ranges; size_t count; } qa_qvm_committed_write;
typedef bool (*qa_qvm_write_observer)(void *, qa_qvm *, const qa_qvm_committed_write *, qa_error *);
/* Publish callbacks may only perform bookkeeping. After-publication callbacks
 * may reenter the VM; event byte snapshots remain stable for the delivery. */
bool qa_qvm_observe_writes(qa_qvm *, const qa_qvm_write_range *, size_t,
                          qa_qvm_write_observer publish, qa_qvm_write_observer after,
                          void *, qa_qvm_binding *, qa_error *);
bool qa_qvm_unobserve_writes(qa_qvm *, qa_qvm_binding, qa_error *);

/* Checkpoints are portable explicit envelopes; no host structs or suspended
 * native call stacks are serialized. Restore requires the same image/profile.
 * Host checkpoint/restore callbacks cannot invoke, restart, or destroy the VM.
 * A host restore failure is reported after RAM commits, matching source order. */
bool qa_qvm_checkpoint(qa_qvm *, qa_buffer *, qa_error *);
bool qa_qvm_restore(qa_qvm *, qa_bytes checkpoint, qa_error *);
const char *qa_qvm_role_name(qa_qvm_role);
bool qa_qvm_validate_ui(qa_qvm *, int32_t *api_version, qa_error *);

/* ABI records use the existing native Q3 protocol and collision structures.
 * source_tags preserves mod-private enum values; presentation mode translates
 * the legacy public enums and rejects tags it cannot represent. */
typedef struct qa_q3_entity qa_q3_entity;
typedef struct qa_q3_player qa_q3_player;
typedef struct qa_q3_usercmd qa_q3_usercmd;
typedef struct qa_q3_gamestate qa_q3_gamestate;
typedef struct qa_q3_snapshot qa_q3_snapshot;
typedef struct qa_trace_result qa_trace_result;
size_t qa_qvm_entity_bytes(qa_qvm_abi);
size_t qa_qvm_player_bytes(qa_qvm_abi);
size_t qa_qvm_shared_entity_bytes(qa_qvm_abi);
size_t qa_qvm_snapshot_bytes(qa_qvm_abi);
bool qa_qvm_event_tag(qa_qvm_abi, int32_t, bool to_source, int32_t *, qa_error *);
bool qa_qvm_entity_tag(qa_qvm_abi, int32_t, bool to_source, int32_t *, qa_error *);
bool qa_qvm_configstring_tag(qa_qvm_abi, int32_t, int32_t *, qa_error *);
/* Receiver exposure for canonical reached commands; lexical console argv
 * keeps its original indices. */
bool qa_qvm_client_configstring_argument(qa_qvm_abi, const char *, int32_t *, bool *mapped, qa_error *);
bool qa_qvm_read_entity(qa_qvm *, int32_t pointer, bool source_tags, qa_q3_entity *, qa_error *);
bool qa_qvm_write_entity(qa_qvm *, int32_t pointer, bool source_tags, const qa_q3_entity *, qa_error *);
/* The guest ABI does not encode product identity; read_player initializes the
 * native protocol metadata to Arena. Hosts set the selected product afterward. */
bool qa_qvm_read_player(qa_qvm *, int32_t pointer, bool source_tags, qa_q3_player *, qa_error *);
bool qa_qvm_write_player(qa_qvm *, int32_t pointer, bool source_tags, bool preserve_private,
                          const qa_q3_player *, qa_error *);
bool qa_qvm_read_usercmd(qa_qvm *, int32_t pointer, qa_q3_usercmd *, qa_error *);
bool qa_qvm_write_usercmd(qa_qvm *, int32_t pointer, bool preserve_private, const qa_q3_usercmd *, qa_error *);
bool qa_qvm_write_gamestate(qa_qvm *, int32_t pointer, bool source_tags, const qa_q3_gamestate *, qa_error *);
bool qa_qvm_write_snapshot(qa_qvm *, int32_t pointer, bool source_tags, const qa_q3_snapshot *, int32_t ping, qa_error *);
bool qa_qvm_write_trace(qa_qvm *, int32_t pointer, const qa_trace_result *, int32_t entity_number, qa_error *);
typedef struct qa_qvm_entity_shared {
    bool linked, inline_model;
    int32_t linkcount, server_flags, single_client, contents, owner_number;
    qa_bounds local_bounds, absolute_bounds;
    qa_vec3 origin, angles;
} qa_qvm_entity_shared;
bool qa_qvm_read_shared_entity(qa_qvm *, int32_t pointer, qa_qvm_entity_shared *, qa_error *);
bool qa_qvm_write_shared_entity(qa_qvm *, int32_t pointer, const qa_qvm_entity_shared *, qa_error *);

#endif
