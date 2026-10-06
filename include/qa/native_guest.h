#ifndef QA_NATIVE_GUEST_H
#define QA_NATIVE_GUEST_H

#include "qa/native.h"
#include "qa/source_save.h"

typedef struct qa_native_guest qa_native_guest;
typedef struct guest_profile_guard_launch guest_profile_guard_launch;

enum { QA_NATIVE_GUEST_PAGE = 4096, QA_NATIVE_GUEST_READ = 1,
       QA_NATIVE_GUEST_WRITE = 2, QA_NATIVE_GUEST_EXECUTE = 4 };

typedef struct qa_native_guest_table {
    uint16_t selector;
    uint64_t base;
    uint32_t limit, flags;
} qa_native_guest_table;

/* Physical FP registers retain their 80-bit representations, independently of
 * TOP. Vector words are architectural little-endian lanes, not host doubles. */
typedef struct qa_native_guest_cpu {
    uint64_t registers[QA_NATIVE_REGISTER_COUNT];
    uint64_t instruction, flags;
    qa_native_guest_table segments[6]; /* Actual cached CS, DS, ES, SS, FS, GS. */
    qa_native_guest_table tables[4]; /* GDTR, IDTR, LDTR, TR. */
    uint64_t control[9], debug[8], efer, xcr0, xstate_bv;
    uint32_t execution_flags, execution_flags2, a20_mask;
    uint64_t fp_mantissa[8];
    uint16_t fp_exponent[8], fp_control, fp_status, fp_tags;
    uint64_t fp_instruction, fp_operand;
    uint16_t fp_code_selector, fp_data_selector, fp_opcode;
    uint32_t mxcsr;
    uint64_t xmm[16][2];
} qa_native_guest_cpu;

typedef enum qa_native_guest_backend {
    QA_NATIVE_GUEST_EMULATED, QA_NATIVE_GUEST_HOST_X86_64
} qa_native_guest_backend;
typedef struct qa_native_guest_options {
    qa_native_image_info image;
    uint64_t allocation_base;
    size_t maximum_backing_bytes;
    qa_native_guest_backend backend;
    /* Actual executable containing the child bootstrap, borrowed through
     * create/restore only. It is never an artifact or saved runtime identity. */
    const char *host_executable;
    /* Actual configured instruction monitor, borrowed through create/restore.
     * Installation is proved by the physical child, not by these paths. */
    const guest_profile_guard_launch *profile_guard;
} qa_native_guest_options;

typedef struct qa_native_guest_mapping {
    uint64_t id, base, bytes, backing, backing_offset;
    uint32_t permissions;
} qa_native_guest_mapping;

/* A private file view owns its snapshot. The final partial file page is
 * accessible and zero padded; whole pages beyond EOF always fault, including
 * through aliases and after protection changes. Execution uses owned mapped
 * pages; cold reconstruction borrows the actual resource owner's baseline. */
typedef struct qa_native_guest_file {
    uint64_t bytes, offset, accessible_bytes, capability;
} qa_native_guest_file;
typedef enum qa_native_guest_fault_kind {
    QA_NATIVE_GUEST_FAULT_UNMAPPED, QA_NATIVE_GUEST_FAULT_PROTECTION,
    QA_NATIVE_GUEST_FAULT_FILE_EOF
} qa_native_guest_fault_kind;
typedef struct qa_native_guest_fault {
    qa_native_guest_fault_kind kind;
    uint32_t access;
    uint64_t address, backing, backing_offset;
} qa_native_guest_fault;

/* Callback addresses identify caller-owned executable entries. A
 * callback decodes its genuine ABI and updates the real CPU continuation.
 * The engine is stopped before dispatch, so nested guest execution is legal.
 * Binding preserves original entry bytes; shared imports own their trap slots.
 * Returning at the same instruction is an error, never a fabricated result. */
typedef bool (*qa_native_guest_callback_fn)(void *, qa_native_guest *, uint64_t, qa_error *);
typedef struct qa_native_guest_callback {
    uint64_t id, address;
    qa_native_guest_callback_fn invoke;
    void *context;
} qa_native_guest_callback;
typedef bool (*qa_native_guest_callback_resolve_fn)(void *, uint64_t, uint64_t,
    qa_native_guest_callback *, qa_error *);
/* Synchronous stopped instruction boundary, after entry callback dispatch and
 * before the original instruction. A changed real IP redirects execution;
 * an unchanged IP executes the original instruction exactly once. */
typedef bool (*qa_native_guest_instruction_fn)(void *, qa_native_guest *, uint64_t, qa_error *);
bool qa_native_guest_instructions(qa_native_guest *, qa_native_guest_instruction_fn, void *, qa_error *);

typedef struct qa_native_guest_commit {
    uint64_t address, backing, backing_offset;
    size_t bytes;
    uint64_t instruction;
    bool instruction_last;
} qa_native_guest_commit;
/* Reports committed RAM stores, including faulted instruction prefixes and
 * same-value stores. Aliases
 * share backing identity. Observers run with the CPU stopped before the next
 * source instruction. An observer draining a CPU fault can read but cannot
 * mutate or resume the owner. Host writes use the same publication boundary. */
typedef bool (*qa_native_guest_commit_fn)(void *, qa_native_guest *,
    const qa_native_guest_commit *, qa_error *);

/* Emulated x86 uses pinned Unicorn 2.1.4 with explicit cached-segment state
 * and committed-store extensions. Qualified Linux x64 hardware execution uses
 * an isolated child with owned fixed mappings, imports and full user CPU state.
 * Backend selection is explicit and never changes after construction. The
 * enclosing artifact/runtime profile supplies actual stack/TLS/OS ownership
 * and qualifies syscall/instruction semantics before source initialization.
 * All operations stay on the owner's thread. Failed create/restore can return
 * a retained failed owner only when its actual CPU close was rejected. */
bool qa_native_guest_create(const qa_native_guest_options *, qa_native_guest **, qa_error *);
bool qa_native_guest_destroy(qa_native_guest **, qa_error *);
bool qa_native_guest_idle(const qa_native_guest *);
bool qa_native_guest_terminal(const qa_native_guest *);
qa_native_guest_backend qa_native_guest_execution(const qa_native_guest *);
bool qa_native_guest_map(qa_native_guest *, uint64_t, size_t, uint32_t, qa_bytes,
    qa_native_guest_mapping *, qa_error *);
bool qa_native_guest_map_file(qa_native_guest *, uint64_t, size_t, uint32_t,
    qa_bytes, uint64_t, qa_native_guest_mapping *, qa_error *);
bool qa_native_guest_map_file_source(qa_native_guest *, uint64_t, size_t, uint32_t,
    const qa_source_save_memory_source *, size_t file_bytes, uint64_t offset,
    uint64_t capability, qa_native_guest_mapping *, qa_error *);
bool qa_native_guest_file_backing(const qa_native_guest *, uint64_t,
    qa_native_guest_file *, qa_error *);
/* Read the real CPU access fault even on a terminal owner. False means this
 * owner has not recorded a CPU memory fault; no signal handler is fabricated. */
bool qa_native_guest_last_fault(const qa_native_guest *, qa_native_guest_fault *);
bool qa_native_guest_alias(qa_native_guest *, uint64_t, size_t, uint32_t,
    uint64_t, size_t, qa_native_guest_mapping *, qa_error *);
bool qa_native_guest_unmap(qa_native_guest *, uint64_t, qa_error *);
bool qa_native_guest_protect(qa_native_guest *, uint64_t, uint32_t, qa_error *);
/* Page-aligned ranges split real mappings while retaining backing aliases.
 * The first surviving fragment retains its ID; further fragments get fresh
 * IDs. Enumerate current mappings after a change instead of caching extents.
 * Removing owned allocator storage requires free. A dependency failure during
 * replacement makes the owner terminal and requires whole-owner destruction. */
bool qa_native_guest_protect_range(qa_native_guest *, uint64_t, size_t, uint32_t, qa_error *);
bool qa_native_guest_unmap_range(qa_native_guest *, uint64_t, size_t, qa_error *);
size_t qa_native_guest_mapping_count(const qa_native_guest *);
bool qa_native_guest_mapping_at(const qa_native_guest *, size_t,
    qa_native_guest_mapping *, qa_error *);
bool qa_native_guest_read(const qa_native_guest *, uint64_t, void *, size_t, qa_error *);
bool qa_native_guest_write(qa_native_guest *, uint64_t, qa_bytes, qa_error *);
/* Byte heap allocations have 16-byte alignment and exact requested ownership.
 * Page ownership, mapping permissions and explicit alignment use the aligned API. */
bool qa_native_guest_allocate(qa_native_guest *, size_t, int32_t, uint64_t *, qa_error *);
/* Alignment is the actual requested power of two. Only the requested page
 * extent is backed; alignment gaps consume addresses, not backing storage. */
bool qa_native_guest_allocate_aligned(qa_native_guest *, size_t, size_t, uint32_t,
    int32_t, uint64_t *, qa_error *);
bool qa_native_guest_free(qa_native_guest *, uint64_t, qa_error *);
bool qa_native_guest_allocation(const qa_native_guest *, uint64_t,
    qa_native_allocation_info *, qa_error *);
bool qa_native_guest_cpu_read(const qa_native_guest *, qa_native_guest_cpu *, qa_error *);
bool qa_native_guest_cpu_write(qa_native_guest *, const qa_native_guest_cpu *, qa_error *);
bool qa_native_guest_bind(qa_native_guest *, const qa_native_guest_callback *, qa_error *);
bool qa_native_guest_unbind(qa_native_guest *, uint64_t, qa_error *);
bool qa_native_guest_observe(qa_native_guest *, qa_native_guest_commit_fn, void *, qa_error *);
/* Exact stop address, shared nested instruction budget, no host wall clock.
 * A CPU or callback fault makes this owner terminal; it cannot be checkpointed.
 * A caller prepares the actual ABI stack/registers before this raw operation. */
bool qa_native_guest_run(qa_native_guest *, uint64_t, uint64_t, size_t, qa_error *);
/* Invoke the original instruction at a genuinely bound entry exactly once.
 * Recursion reaches the binding again. Hardware calls use budget zero and the
 * same one-shot bypass in their actual instruction monitor. */
bool qa_native_guest_run_original(qa_native_guest *, uint64_t, uint64_t, uint64_t,
    size_t, qa_error *);
/* Explicit hardware invocation without instruction budgets. Watched stores
 * and admitted instruction boundaries use the actual child monitor. It
 * observes genuine bound entries without
 * rewriting source bytes and dispatches their actual IDs and stopped ABI state. */
bool qa_native_guest_run_native(qa_native_guest *, uint64_t, uint64_t, qa_error *);

typedef struct qa_native_guest_syscall {
    uint64_t instruction, next_instruction, number, arguments[6];
} qa_native_guest_syscall;
typedef struct qa_native_guest_syscall_result {
    int64_t value;
    bool stop;
} qa_native_guest_syscall_result;
typedef bool (*qa_native_guest_syscall_fn)(void *, qa_native_guest *,
    const qa_native_guest_syscall *, qa_native_guest_syscall_result *, qa_error *);
/* Linux x64 PROGRAM execution with an actual kernel-service owner. A genuine
 * syscall stops before any host syscall. A successful returning service commits
 * RAX/RCX/R11 and its two-byte source continuation; stop retains the stopped
 * source CPU for the kernel owner's exit receipt. EMULATED requires a positive
 * shared budget; HOST requires zero. stop may be zero for a program with no
 * return continuation. No syscall authority leaks into nested
 * ordinary library invocations. */
bool qa_native_guest_run_program(qa_native_guest *, uint64_t, uint64_t, size_t,
    qa_native_guest_syscall_fn, void *, bool *, qa_error *);

/* Captures this lower owner's actual CPU, mappings/backing aliases, allocation
 * records/cursor and stable callback IDs. Runtime, image-loader, TLS allocator,
 * destructors and OS-handle owners require their own enclosing records.
 * Restore creates an inert candidate and binds callbacks by ID without calling
 * them. It never invokes source initializers, dllEntry, Init or game save APIs. */
/* Borrow the installed loader baseline for an actual backing. Non-image RAM
 * has an empty span and an implicit zero tail. File pages require their real
 * installed or opened-resource baseline, never a saved copy of file bytes. */
typedef struct qa_native_guest_pristine {
    qa_source_save_memory_source memory;
    /* Called with memory.context after delta I/O, including failed reads. */
    bool (*release)(void *, qa_error *);
} qa_native_guest_pristine;
typedef struct qa_native_guest_baseline {
    bool (*read)(void *, uint64_t backing, size_t extent,
        const qa_native_guest_file *, qa_native_guest_pristine *, qa_error *);
    void *context;
} qa_native_guest_baseline;
bool qa_native_guest_checkpoint(qa_native_guest *, const qa_native_guest_baseline *,
    qa_buffer *, qa_error *);
bool qa_native_guest_restore(qa_bytes, const qa_native_guest_options *,
    qa_native_guest_callback_resolve_fn, void *, const qa_native_guest_baseline *,
    qa_native_guest **, qa_error *);

#endif
