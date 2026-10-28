/*
 * Port of src/guest/runtime/common/format.ts: UCRT/glibc narrow printf over
 * checked guest bytes. The TS parsed-format retention cache is omitted
 * (formats are reparsed; unobservable). GuestFormatFortifyFailure aborts
 * with the same message (uncaught in TS). String bodies are byte strings,
 * so %c with value 0 emits one NUL byte like the TS length-based output.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_COMMON_FORMAT_H
#define QA_GUEST_RUNTIME_COMMON_FORMAT_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/guest/abi/values.h"
#include "qa/guest/runtime/common/format/arguments.h"
#include "qa/guest/runtime/common/format/float.h"

typedef enum qa_guest_format_termination {
  QA_TERMINATION_C99 = 0, QA_TERMINATION_UCRT, QA_TERMINATION_UCRT_LEGACY
} qa_guest_format_termination;

typedef struct qa_guest_format_request {
  qa_mapped_guest_memory *memory;
  qa_mapped_memory_ext *memory_ext;
  qa_format_dialect dialect;
  bool has_format;
  qa_guest_core_address format;
  bool has_arguments;
  qa_guest_core_address arguments;
  bool has_buffer;
  qa_guest_core_address buffer;
  int64_t capacity;
  qa_guest_format_termination termination;
  bool continue_count;
  qa_format_rounding rounding;
  int exponent_digits; /* 2|3 */
  bool fortify;
} qa_guest_format_request;

typedef struct qa_guest_format_result {
  int result;
  bool has_errno;
  int errno_value;
} qa_guest_format_result;

/* TS throw new GuestFormatFortifyFailure(message). */
void qa_guest_format_fortify_failure(const char *message);

qa_guest_format_result qa_format_guest_buffer(const qa_guest_format_request *request);

#endif
