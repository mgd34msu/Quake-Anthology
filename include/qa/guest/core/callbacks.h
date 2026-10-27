/*
 * Port of src/guest/core/callbacks.ts: GuestCallbackTable.
 * C accommodations: the table requires the concrete sparse memory (it
 * allocates/protects trap pages through ops the registers.h vtable
 * does not carry). TS removal closures become opaque handles plus
 * unbind functions. entryRevision (a fresh Symbol per change) becomes
 * a monotonically increasing counter compared by value. Callback ids
 * are C strings compared by value, as TS compares string ids.
 * Plain TS Errors abort with the identical message; the one TS
 * RangeError aborts through qa_range_error as usual.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_CORE_CALLBACKS_H
#define QA_GUEST_CORE_CALLBACKS_H

#include "qa/guest/core/contracts.h"
#include "qa/guest/core/memory.h"

typedef enum qa_guest_callback_binding { QA_CALLBACK_BOUND = 0, QA_CALLBACK_UNBOUND } qa_guest_callback_binding;

typedef struct qa_saved_host_callback_address {
  qa_callback_id id;
  uint64_t byte_offset;
  qa_guest_call_signature signature;
  qa_guest_callback_binding binding;
} qa_saved_host_callback_address;

typedef struct qa_guest_callback_entry qa_guest_callback_entry; /* opaque handle target */
typedef uint64_t qa_guest_entry_handle; /* 0 = none */

typedef struct qa_guest_callback_table {
  qa_sparse_guest_memory *memory;
  bool managed;
  /* private */
  qa_guest_callback_entry *entries; /* list via callbacks.c node layout */
  void *observer_buckets;           /* callbacks.c list */
  uint64_t entry_revision;
  uint64_t next_handle;
} qa_guest_callback_table;

void qa_guest_callback_table_init(qa_guest_callback_table *table, qa_sparse_guest_memory *memory);
void qa_guest_callback_table_init_managed(qa_guest_callback_table *table, qa_sparse_guest_memory *memory);
void qa_guest_callback_table_destroy(qa_guest_callback_table *table);
bool qa_guest_callback_table_managed(const qa_guest_callback_table *table);
uint64_t qa_guest_callback_table_entry_revision(const qa_guest_callback_table *table);
bool qa_guest_callback_table_instruction_unhooked(const qa_guest_callback_table *table, uint64_t byte_offset);

/* accepts: NULL for plain bind() entries. Returns a removal handle. */
qa_guest_entry_handle qa_guest_callback_table_bind_entry(qa_guest_callback_table *table, qa_guest_address address,
                                                         const qa_guest_host_callback *callback,
                                                         bool (*accepts)(void *ctx), void *accepts_ctx);
void qa_guest_callback_table_unbind_entry(qa_guest_callback_table *table, qa_guest_entry_handle handle);
qa_guest_entry_handle qa_guest_callback_table_observe_entry(qa_guest_callback_table *table, qa_guest_address address,
                                                            void (*before)(void *ctx), void *ctx);
void qa_guest_callback_table_unobserve_entry(qa_guest_callback_table *table, qa_guest_entry_handle handle);
bool qa_guest_callback_table_enter(qa_guest_callback_table *table, qa_guest_address address);
qa_guest_address qa_guest_callback_table_bind(qa_guest_callback_table *table, const qa_guest_host_callback *callback);
void qa_guest_callback_table_unbind(qa_guest_callback_table *table, qa_callback_id id);
bool qa_guest_callback_table_address(const qa_guest_callback_table *table, qa_callback_id id, qa_guest_address *out);
bool qa_guest_callback_table_has_bound_trap(const qa_guest_callback_table *table, uint64_t byte_offset);
const qa_guest_host_callback *qa_guest_callback_table_resolve(qa_guest_callback_table *table, qa_guest_address address);
qa_call_result qa_guest_callback_table_invoke(qa_guest_callback_table *table, qa_guest_address address,
                                                    const qa_guest_call_context *context,
                                                    const qa_call_value *arguments, size_t argument_count);
/* Malloc'd array; caller frees the array only. Order is bind order
 * (TS Map insertion order over #byId). */
qa_saved_host_callback_address *qa_guest_callback_table_checkpoint(const qa_guest_callback_table *table, size_t *out_count);
/* resolve returns NULL for a missing bound callback (TS null). */
void qa_guest_callback_table_restore(qa_guest_callback_table *table, qa_sparse_guest_memory *memory,
                                     const qa_saved_host_callback_address *saved, size_t count,
                                     const qa_guest_host_callback *(*resolve)(void *ctx, qa_callback_id id), void *resolve_ctx);

#endif
