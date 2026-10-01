#ifndef QA_NATIVE_GUEST_H
#define QA_NATIVE_GUEST_H

#include "qa/native.h"

typedef struct qa_native_guest qa_native_guest;

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

typedef struct qa_native_guest_options {
    qa_native_image_info image;
    uint64_t allocation_base;
    size_t maximum_backing_bytes;
} qa_native_guest_options;

typedef struct qa_native_guest_mapping {
    uint64_t id, base, bytes, backing, backing_offset;
    uint32_t permissions;
} qa_native_guest_mapping;

/* Callback addresses identify caller-installed executable trap slots. A
 * callback decodes its genuine ABI and updates the real CPU continuation.
 * The engine is stopped before dispatch, so nested guest execution is legal.
 * Returning at the same instruction is an error, never a fabricated result. */
typedef bool (*qa_native_guest_callback_fn)(void *, qa_native_guest *, uint64_t, qa_error *);
typedef struct qa_native_guest_callback {
    uint64_t id, address;
    qa_native_guest_callback_fn invoke;
    void *context;
} qa_native_guest_callback;
typedef bool (*qa_native_guest_callback_resolve_fn)(void *, uint64_t, uint64_t,
    qa_native_guest_callback *, qa_error *);

typedef struct qa_native_guest_commit {
    uint64_t address, backing, backing_offset;
    size_t bytes;
} qa_native_guest_commit;
/* Reports committed RAM stores, including faulted instruction prefixes and
 * same-value stores. Aliases
 * share backing identity. Observers run with the CPU stopped before the next
 * source instruction. An observer draining a CPU fault can read but cannot
 * mutate or resume the owner. Host writes use the same publication boundary. */
typedef bool (*qa_native_guest_commit_fn)(void *, qa_native_guest *,
    const qa_native_guest_commit *, qa_error *);

/* This lower owner executes x86 through pinned Unicorn 2.1.4 with the explicit
 * cached-segment state and committed-store extensions. It does not load a module or
 * claim a native ABI/runtime profile. Native invocation additionally requires
 * owned stack/TLS, imports, OS resources and a qualified native entry bridge.
 * All operations stay on the owner's thread. Failed create/restore can return
 * a retained failed owner only when its actual CPU close was rejected. */
bool qa_native_guest_create(const qa_native_guest_options *, qa_native_guest **, qa_error *);
bool qa_native_guest_destroy(qa_native_guest **, qa_error *);
bool qa_native_guest_idle(const qa_native_guest *);
bool qa_native_guest_map(qa_native_guest *, uint64_t, size_t, uint32_t, qa_bytes,
    qa_native_guest_mapping *, qa_error *);
bool qa_native_guest_alias(qa_native_guest *, uint64_t, size_t, uint32_t,
    uint64_t, size_t, qa_native_guest_mapping *, qa_error *);
bool qa_native_guest_unmap(qa_native_guest *, uint64_t, qa_error *);
bool qa_native_guest_protect(qa_native_guest *, uint64_t, uint32_t, qa_error *);
size_t qa_native_guest_mapping_count(const qa_native_guest *);
bool qa_native_guest_mapping_at(const qa_native_guest *, size_t,
    qa_native_guest_mapping *, qa_error *);
bool qa_native_guest_read(const qa_native_guest *, uint64_t, void *, size_t, qa_error *);
bool qa_native_guest_write(qa_native_guest *, uint64_t, qa_bytes, qa_error *);
bool qa_native_guest_allocate(qa_native_guest *, size_t, int32_t, uint64_t *, qa_error *);
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

/* Captures this lower owner's actual CPU, mappings/backing aliases, allocation
 * records/cursor and stable callback IDs. Runtime, image-loader, TLS allocator,
 * destructors and OS-handle owners require their own enclosing records.
 * Restore creates an inert candidate and binds callbacks by ID without calling
 * them. It never invokes source initializers, dllEntry, Init or game save APIs. */
bool qa_native_guest_checkpoint(qa_native_guest *, qa_buffer *, qa_error *);
bool qa_native_guest_restore(qa_bytes, const qa_native_guest_options *,
    qa_native_guest_callback_resolve_fn, void *, qa_native_guest **, qa_error *);

#endif
