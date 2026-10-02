#ifndef QA_NATIVE_GUEST_SYSV_LIBC_FORMAT_FLOAT_H
#define QA_NATIVE_GUEST_SYSV_LIBC_FORMAT_FLOAT_H
#include "qa/common.h"

typedef struct sysv_format_binary {
    uint64_t coefficient;
    int exponent;
    unsigned kind;
    bool negative, denormal, extended, signaling, indefinite;
} sysv_format_binary;
/* kind 0 finite, 1 infinity, 2 NaN/unsupported x87 representation. */
sysv_format_binary sysv_format_decode(uint64_t, uint16_t, bool);
bool sysv_format_float(sysv_format_binary, char, int, bool, unsigned,
    qa_buffer *, qa_error *);
bool sysv_format_float_dialect(sysv_format_binary, char, int, bool, unsigned,
    bool, qa_buffer *, qa_error *);
#endif
