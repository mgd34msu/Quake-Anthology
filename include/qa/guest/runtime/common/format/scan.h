/*
 * Port of src/guest/runtime/common/format/scan.ts: the supported subset of
 * Windows sscanf over a guest buffer. UnsupportedGuestScan (caught by
 * services.ts and rethrown as UnsupportedWindowsImport) rides a setjmp
 * channel: pass a catch context to intercept it, or NULL to abort with
 * the same message. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_COMMON_FORMAT_SCAN_H
#define QA_GUEST_RUNTIME_COMMON_FORMAT_SCAN_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"

typedef struct qa_guest_scan_catch {
  jmp_buf buf;
  char message[512];
} qa_guest_scan_catch;

typedef struct qa_scan_windows_buffer_options {
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  qa_guest_core_address input;
  uint64_t capacity; /* TS bigint: an unsigned size_t (SIZE_MAX for sscanf) */
  qa_guest_core_address format;
  bool has_arguments;
  qa_guest_core_address arguments;
} qa_scan_windows_buffer_options;

int qa_scan_windows_buffer(const qa_scan_windows_buffer_options *options, qa_guest_scan_catch *catch_);

#endif
