/*
 * Port of src/guest/runtime/common/memory.ts: guest argument accessors and
 * memory helpers. Managed SparseGuestMemory fast paths ride the ext
 * move/fill/find_zero hooks plus managed_sparse (registers.h); otherwise
 * the generic copy/write path runs, exactly like the TS fallbacks.
 * Strings: narrow strings are malloc'd byte strings (TS latin1 semantics);
 * wide strings return malloc'd UTF-16 unit arrays. Buffer.from latin1 and
 * String.fromCharCode agree on bytes 0..255. GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_COMMON_MEMORY_H
#define QA_GUEST_RUNTIME_COMMON_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"

const qa_call_value *qa_guest_argument(const qa_call_value *args, size_t count, size_t index);
uint64_t qa_guest_integer_u64(const qa_call_value *args, size_t count, size_t index);
int64_t qa_guest_integer(const qa_call_value *args, size_t count, size_t index);
size_t qa_guest_count(const qa_call_value *args, size_t count, size_t index);
bool qa_guest_pointer_argument(const qa_call_value *args, size_t count, size_t index, qa_guest_core_address *out);
qa_guest_core_address qa_guest_required_pointer(const qa_call_value *args, size_t count, size_t index);

uint64_t qa_read_unsigned(const qa_mapped_guest_memory *memory, qa_guest_core_address address, int width);
void qa_write_unsigned(const qa_mapped_guest_memory *memory, qa_guest_core_address address, int width, uint64_t value);
bool qa_read_pointer(const qa_mapped_guest_memory *memory, qa_guest_core_address address, qa_guest_core_address *out);
void qa_write_pointer(const qa_mapped_guest_memory *memory, qa_guest_core_address address,
                      bool has, qa_guest_core_address value);
void qa_move_bytes(const qa_mapped_guest_memory *memory, const qa_mapped_memory_ext *ext,
                   qa_guest_core_address destination, qa_guest_core_address source, size_t byte_length);
void qa_fill_bytes(const qa_mapped_guest_memory *memory, const qa_mapped_memory_ext *ext,
                   qa_guest_core_address address, size_t byte_length, int value);
size_t qa_string_length(const qa_mapped_guest_memory *memory, const qa_mapped_memory_ext *ext,
                        qa_guest_core_address address, size_t maximum);
/* Malloc'd; caller frees. */
char *qa_read_string(const qa_mapped_guest_memory *memory, const qa_mapped_memory_ext *ext,
                     qa_guest_core_address address, size_t maximum);
uint16_t *qa_read_string_wide(const qa_mapped_guest_memory *memory, qa_guest_core_address address,
                              size_t maximum, size_t *out_length);
/* Malloc'd bytes including the terminator; *out_len set. value is a byte
 * string (narrow) whose units widen when wide=true, as TS charCodeAt does
 * for the latin1 strings readString produces. */
uint8_t *qa_string_bytes(const char *value, bool wide, size_t *out_len);
uint8_t *qa_string_bytes_wide(const uint16_t *value, size_t length, bool wide, size_t *out_len);

size_t qa_native_allocation_bytes(size_t logical_bytes);
qa_guest_core_address qa_allocate_native_memory(const qa_mapped_memory_ext *ext, size_t logical_bytes,
                                           const char *label);

#endif
