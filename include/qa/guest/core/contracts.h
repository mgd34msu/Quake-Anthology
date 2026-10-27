/*
 * Port of src/guest/core/contracts.ts (types only): mapped-memory,
 * processor, exception/stop, callback, ABI and image shapes.
 * The shared execution/call/image shapes are owned by
 * qa/guest/abi/values.h and qa/guest/abi/adapter.h (included below);
 * this header adds only what those do not carry: map/allocation
 * options, the memory snapshot, thread TLS, and the guest throw
 * channel. (An earlier draft duplicated the shared shapes with
 * incompatible layouts; the duplicates were removed so every guest
 * module composes in one translation unit, as in TS.)
 * Throw channel: TS GuestMemoryFault / X86ProcessorFault /
 * X64ProcessorFault / Unsupported* throws become a longjmp through
 * qa_guest_throw when a CPU run installs one, else an abort with the
 * identical message (uncaught-throw behavior).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_CORE_CONTRACTS_H
#define QA_GUEST_CORE_CONTRACTS_H

#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h" /* qa_callback_id */
#include "qa/contracts/math.h"     /* qa_vec3, qa_byte_view */
#include "qa/guest/abi/adapter.h"  /* qa_guest_cpu/stop/exception + values.h shapes */
#include "qa/guest/core/registers.h"

/* contracts.ts ModuleIdentity spelling used by the core modules. */
typedef qa_guest_module_identity qa_guest_module_ref;

typedef struct qa_guest_written_range { int64_t byte_offset; int64_t byte_length; } qa_guest_written_range;

const char *qa_guest_access_name(qa_guest_access access);
bool qa_guest_permissions_allows(qa_guest_permissions permissions, qa_guest_access access);

typedef struct qa_guest_map_options {
  uint64_t base;
  size_t byte_length;
  qa_guest_permissions permissions;
  const char *label;          /* NULL = TS undefined ("") */
  const uint8_t *bytes;       /* NULL = TS undefined */
  size_t bytes_length;
} qa_guest_map_options;

typedef struct qa_guest_allocation_options {
  size_t byte_length;
  uint64_t alignment;         /* 0 = TS undefined (16) */
  qa_guest_permissions permissions;
  bool has_permissions;       /* false = TS undefined ("read-write") */
  const char *label;          /* NULL = TS undefined ("allocation") */
} qa_guest_allocation_options;

/* One snapshot mapping: GuestMapping & { backing, backingOffset }. */
typedef struct qa_guest_snapshot_mapping {
  uint64_t base;
  size_t byte_length;
  qa_guest_permissions permissions;
  char *label; /* owned by the snapshot */
  size_t backing;
  size_t backing_offset;
} qa_guest_snapshot_mapping;

typedef struct qa_guest_memory_snapshot {
  qa_guest_module_ref module; /* strings borrowed */
  int pointer_bytes;          /* 4 | 8 */
  uint64_t allocation_base;
  uint8_t **backings;         /* owned copies */
  size_t *backing_lengths;
  size_t backing_count;
  qa_guest_snapshot_mapping *mappings;
  size_t mapping_count;
} qa_guest_memory_snapshot;
void qa_guest_memory_snapshot_free(qa_guest_memory_snapshot *snapshot);

/* TS ReadonlyMap<ModuleIdentity, GuestAddress> -> parallel arrays. */
typedef struct qa_guest_thread_tls {
  uint64_t thread_id;
  qa_guest_address thread_pointer;
  const qa_guest_module_ref *modules;
  const qa_guest_address *addresses;
  size_t module_count;
} qa_guest_thread_tls;

/* --- throw channel (memory.ts GuestMemoryFault + processor faults) --- */
typedef enum qa_guest_memory_fault_reason {
  QA_FAULT_FOREIGN_ADDRESS_SPACE = 0, QA_FAULT_ADDRESS_OVERFLOW, QA_FAULT_NULL_ADDRESS,
  QA_FAULT_INVALID_LENGTH, QA_FAULT_UNMAPPED, QA_FAULT_PERMISSION, QA_FAULT_OVERLAP,
  QA_FAULT_NONCONTIGUOUS_BORROW
} qa_guest_memory_fault_reason;
const char *qa_guest_memory_fault_reason_name(qa_guest_memory_fault_reason reason);

/* TS GuestMemoryFault.access: GuestAccess | "map" | "borrow". */
typedef enum qa_guest_fault_access {
  QA_FAULT_ACCESS_READ = 0, QA_FAULT_ACCESS_WRITE, QA_FAULT_ACCESS_EXECUTE,
  QA_FAULT_ACCESS_MAP, QA_FAULT_ACCESS_BORROW
} qa_guest_fault_access;
const char *qa_guest_fault_access_name(qa_guest_fault_access access);

typedef struct qa_guest_memory_fault {
  qa_guest_memory_fault_reason reason;
  qa_guest_module_ref module;
  uint64_t address;
  size_t byte_length;
  qa_guest_fault_access access;
  char message[512]; /* exact TS Error.message */
} qa_guest_memory_fault;

typedef enum qa_guest_throw_kind {
  QA_GUEST_THROW_NONE = 0, QA_GUEST_THROW_MEMORY, QA_GUEST_THROW_PROCESSOR, QA_GUEST_THROW_UNSUPPORTED
} qa_guest_throw_kind;

typedef struct qa_guest_throw {
  jmp_buf *buf; /* NULL: throws abort with the TS message */
  qa_guest_throw_kind kind;
  qa_guest_memory_fault memory; /* MEMORY */
  int vector;                   /* PROCESSOR */
  bool has_error_code;
  uint64_t error_code;
  char detail[512];             /* PROCESSOR / UNSUPPORTED message */
} qa_guest_throw;

void qa_guest_throw_init(qa_guest_throw *throw_ctx, jmp_buf *buf);
/* TS `throw new GuestMemoryFault(...)`. detail is the TS detail tail;
 * the full message is formatted exactly as the TS constructor does. */
_Noreturn void qa_guest_throw_memory(qa_guest_throw *throw_ctx, qa_guest_memory_fault_reason reason,
                                     const qa_guest_module_ref *module, uint64_t address,
                                     size_t byte_length, qa_guest_fault_access access, const char *detail);
/* TS X86ProcessorFault/X64ProcessorFault(vector, detail[, errorCode]). */
_Noreturn void qa_guest_throw_processor(qa_guest_throw *throw_ctx, int vector, bool has_error_code,
                                        uint64_t error_code, const char *format, ...);
/* TS UnsupportedX86Instruction/X64Unsupported(detail). */
_Noreturn void qa_guest_throw_unsupported(qa_guest_throw *throw_ctx, const char *format, ...);

#endif
