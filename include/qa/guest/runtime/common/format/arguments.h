/*
 * Port of src/guest/runtime/common/format/arguments.ts: va_list walking for
 * Windows/i386 (by value) and System V x64 (register save descriptor).
 * Float arguments own their decoded binary value (qa_binary_value_free).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_COMMON_FORMAT_ARGUMENTS_H
#define QA_GUEST_RUNTIME_COMMON_FORMAT_ARGUMENTS_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"
#include "qa/guest/floating_point/binary.h"

typedef enum qa_format_dialect { QA_FORMAT_WINDOWS = 0, QA_FORMAT_SYSTEM_V } qa_format_dialect;
const char *qa_format_dialect_name(qa_format_dialect dialect);

typedef enum qa_format_argument_type {
  QA_FORMAT_ARG_INT = 0, QA_FORMAT_ARG_INT64, QA_FORMAT_ARG_POINTER,
  QA_FORMAT_ARG_DOUBLE, QA_FORMAT_ARG_LONG_DOUBLE
} qa_format_argument_type;
const char *qa_format_argument_type_name(qa_format_argument_type type);

typedef struct qa_format_argument {
  bool is_float;
  uint64_t integer;
  qa_binary_value float_value; /* owned when is_float */
} qa_format_argument;
void qa_format_argument_free(qa_format_argument *argument);

typedef struct qa_format_arguments {
  qa_mapped_guest_memory *memory;
  qa_format_dialect dialect;
  bool has_cursor;
  qa_guest_core_address cursor;
  unsigned gp, fp;
  bool has_registers;
  qa_guest_core_address registers;
  bool has_descriptor;
  qa_guest_core_address descriptor;
} qa_format_arguments;

void qa_format_arguments_init(qa_format_arguments *args, qa_mapped_guest_memory *memory,
                              qa_format_dialect dialect, bool has_address, qa_guest_core_address address);
qa_format_argument qa_format_arguments_next(qa_format_arguments *args, qa_format_argument_type type);

#endif
