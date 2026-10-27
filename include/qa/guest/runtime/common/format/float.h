/*
 * Port of src/guest/runtime/common/format/float.ts: printf-style decimal/hex
 * conversion rounding the guest binary value itself. Returns a malloc'd
 * string (caller frees), or NULL on allocation failure. `conversion` is one
 * character as at every TS call site; precision_present=false is TS null.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_COMMON_FORMAT_FLOAT_H
#define QA_GUEST_RUNTIME_COMMON_FORMAT_FLOAT_H

#include <stdbool.h>

#include "qa/guest/floating_point/binary.h"

typedef enum qa_format_rounding {
  QA_FORMAT_NEAREST = 0, QA_FORMAT_DOWN, QA_FORMAT_UP, QA_FORMAT_ZERO, QA_FORMAT_LEGACY_NEAREST
} qa_format_rounding;
const char *qa_format_rounding_name(qa_format_rounding mode);

char *qa_format_float(const qa_binary_value *value, char conversion, bool precision_present, int precision,
                      bool alternate, qa_format_rounding mode, bool windows, bool extended,
                      int exponent_digits);

#endif
