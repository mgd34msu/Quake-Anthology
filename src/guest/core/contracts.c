/* Port of src/guest/core/contracts.ts (types only; the runtime content
 * is the name tables and the throw channel). GPL-2.0-or-later. */
#include "qa/guest/core/contracts.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

const char *qa_guest_access_name(qa_guest_access access) {
  switch (access) {
    case QA_ACCESS_READ: return "read";
    case QA_ACCESS_WRITE: return "write";
    default: return "execute";
  }
}
/* memory.ts allows(). */
bool qa_guest_permissions_allows(qa_guest_permissions permissions, qa_guest_access access) {
  switch (access) {
    case QA_ACCESS_READ:
      return permissions == QA_PERM_READ || permissions == QA_PERM_READ_WRITE ||
             permissions == QA_PERM_READ_EXECUTE || permissions == QA_PERM_READ_WRITE_EXECUTE;
    case QA_ACCESS_WRITE:
      return permissions == QA_PERM_READ_WRITE || permissions == QA_PERM_READ_WRITE_EXECUTE;
    default:
      return permissions == QA_PERM_EXECUTE || permissions == QA_PERM_READ_EXECUTE ||
             permissions == QA_PERM_READ_WRITE_EXECUTE;
  }
}

const char *qa_guest_memory_fault_reason_name(qa_guest_memory_fault_reason reason) {
  switch (reason) {
    case QA_FAULT_FOREIGN_ADDRESS_SPACE: return "foreign-address-space";
    case QA_FAULT_ADDRESS_OVERFLOW: return "address-overflow";
    case QA_FAULT_NULL_ADDRESS: return "null-address";
    case QA_FAULT_INVALID_LENGTH: return "invalid-length";
    case QA_FAULT_UNMAPPED: return "unmapped";
    case QA_FAULT_PERMISSION: return "permission";
    case QA_FAULT_OVERLAP: return "overlap";
    case QA_FAULT_QUOTA: return "quota";
    default: return "noncontiguous-borrow";
  }
}
const char *qa_guest_fault_access_name(qa_guest_fault_access access) {
  switch (access) {
    case QA_FAULT_ACCESS_READ: return "read";
    case QA_FAULT_ACCESS_WRITE: return "write";
    case QA_FAULT_ACCESS_EXECUTE: return "execute";
    case QA_FAULT_ACCESS_MAP: return "map";
    default: return "borrow";
  }
}

void qa_guest_memory_snapshot_free(qa_guest_memory_snapshot *snapshot) {
  if (snapshot == NULL) return;
  for (size_t i = 0; i < snapshot->backing_count; i++) free(snapshot->backings[i]);
  free(snapshot->backings);
  free(snapshot->backing_lengths);
  for (size_t i = 0; i < snapshot->mapping_count; i++) free(snapshot->mappings[i].label);
  free(snapshot->mappings);
  memset(snapshot, 0, sizeof *snapshot);
}

void qa_guest_throw_init(qa_guest_throw *throw_ctx, jmp_buf *buf) {
  memset(throw_ctx, 0, sizeof *throw_ctx);
  throw_ctx->buf = buf;
  throw_ctx->kind = QA_GUEST_THROW_NONE;
}

_Noreturn void qa_guest_throw_memory(qa_guest_throw *throw_ctx, qa_guest_memory_fault_reason reason,
                                     const qa_guest_module_ref *module, uint64_t address,
                                     size_t byte_length, qa_guest_fault_access access, const char *detail) {
  /* TS: `${module.id}: ${access} at 0x${address.toString(16)} (${byteLength} bytes): ${detail}` */
  char message[512];
  (void)snprintf(message, sizeof message, "%s: %s at 0x%llx (%zu bytes): %s",
                 module != NULL && module->id != NULL ? module->id : "",
                 qa_guest_fault_access_name(access), (unsigned long long)address, byte_length, detail);
  if (throw_ctx == NULL || throw_ctx->buf == NULL) qa_range_error("%s", message);
  throw_ctx->kind = QA_GUEST_THROW_MEMORY;
  throw_ctx->memory.reason = reason;
  throw_ctx->memory.module = module != NULL ? *module : (qa_guest_module_ref){0};
  throw_ctx->memory.address = address;
  throw_ctx->memory.byte_length = byte_length;
  throw_ctx->memory.access = access;
  (void)snprintf(throw_ctx->memory.message, sizeof throw_ctx->memory.message, "%s", message);
  longjmp(*throw_ctx->buf, 1);
}

_Noreturn void qa_guest_throw_processor(qa_guest_throw *throw_ctx, int vector, bool has_error_code,
                                        uint64_t error_code, const char *format, ...) {
  char detail[512];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(detail, sizeof detail, format, args);
  va_end(args);
  if (throw_ctx == NULL || throw_ctx->buf == NULL) qa_range_error("%s", detail);
  throw_ctx->kind = QA_GUEST_THROW_PROCESSOR;
  throw_ctx->vector = vector;
  throw_ctx->has_error_code = has_error_code;
  throw_ctx->error_code = error_code;
  (void)snprintf(throw_ctx->detail, sizeof throw_ctx->detail, "%s", detail);
  longjmp(*throw_ctx->buf, 1);
}

_Noreturn void qa_guest_throw_unsupported(qa_guest_throw *throw_ctx, const char *format, ...) {
  char detail[512];
  va_list args;
  va_start(args, format);
  (void)vsnprintf(detail, sizeof detail, format, args);
  va_end(args);
  if (throw_ctx == NULL || throw_ctx->buf == NULL) qa_range_error("%s", detail);
  throw_ctx->kind = QA_GUEST_THROW_UNSUPPORTED;
  (void)snprintf(throw_ctx->detail, sizeof throw_ctx->detail, "%s", detail);
  longjmp(*throw_ctx->buf, 1);
}
