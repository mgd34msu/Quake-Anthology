/*
 * Port of src/contracts/numeric.ts (types only): arithmetic profiles,
 * numeric operation tables, and random-source state shapes.
 * String-union mapping is documented per enumerator.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_NUMERIC_H
#define QA_CONTRACTS_NUMERIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/math.h"

/* TS FloatingRounding. */
typedef enum qa_floating_rounding {
  QA_FLOATING_ROUNDING_NEAREST_EVEN,       /* "nearest-even" */
  QA_FLOATING_ROUNDING_TOWARD_ZERO,        /* "toward-zero" */
  QA_FLOATING_ROUNDING_TOWARD_POSITIVE,    /* "toward-positive" */
  QA_FLOATING_ROUNDING_TOWARD_NEGATIVE     /* "toward-negative" */
} qa_floating_rounding;

/* TS ArithmeticProfile discriminated union, on `kind`. */
typedef enum qa_arithmetic_profile_kind {
  QA_ARITHMETIC_BINARY32,        /* "binary32", round "each-operation" */
  QA_ARITHMETIC_DONOR_BINARY64,  /* "donor-binary64", source q1-ts|q2-ts */
  QA_ARITHMETIC_X87,             /* "x87" */
  QA_ARITHMETIC_SSE              /* "sse" */
} qa_arithmetic_profile_kind;

typedef struct qa_arithmetic_profile {
  qa_arithmetic_profile_kind kind;
  /* donor-binary64 only: TS source "q1-ts" | "q2-ts". */
  const char *source;
  /* x87 only: 24 | 53 | 64. */
  int64_t precision_bits;
  /* x87/sse. */
  qa_floating_rounding rounding;
  /* sse only. */
  bool flush_to_zero;
  bool denormals_are_zero;
} qa_arithmetic_profile;

/* TS FloatToIntProfile. */
typedef enum qa_float_to_int_profile {
  QA_FLOAT_TO_INT_QVM_INDEFINITE,       /* "qvm-indefinite" */
  QA_FLOAT_TO_INT_X86_INDEFINITE,       /* "x86-indefinite" */
  QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION  /* "checked-c-truncation" */
} qa_float_to_int_profile;

typedef const char *qa_numeric_profile_id; /* `${string}:${string}` */

typedef struct qa_numeric_profile {
  qa_numeric_profile_id id;
  qa_arithmetic_profile arithmetic;
  /* TS scalarStorage is the literal "binary32"; integerOverflow "wrap32". */
  qa_float_to_int_profile float_to_int;
} qa_numeric_profile;

/** TS NumericOperations interface: function table + ctx. */
typedef struct qa_numeric_operations {
  void *ctx;
  qa_numeric_profile profile;
  double (*store)(void *ctx, double value);
  double (*add)(void *ctx, double left, double right);
  double (*subtract)(void *ctx, double left, double right);
  double (*multiply)(void *ctx, double left, double right);
  double (*divide)(void *ctx, double left, double right);
  double (*square_root)(void *ctx, double value);
  double (*to_int32)(void *ctx, double value);
  double (*wrap_int32)(void *ctx, double value);
  double (*wrap_uint32)(void *ctx, double value);
} qa_numeric_operations;

typedef struct qa_random_step { double seed; double value; } qa_random_step;

/* TS RandomState discriminated union, on `kind`. */
typedef enum qa_random_state_kind {
  QA_RANDOM_STATE_Q3_LCG,               /* "q3-lcg" */
  QA_RANDOM_STATE_MSVCRT_RAND,          /* "msvcrt-rand" */
  QA_RANDOM_STATE_GLIBC_RANDOM,         /* "glibc-random" */
  QA_RANDOM_STATE_Q2_RERELEASE_MT19937, /* "q2-rerelease-mt19937" */
  QA_RANDOM_STATE_GUEST                 /* "guest" */
} qa_random_state_kind;

typedef struct qa_random_state {
  qa_random_state_kind kind;
  double seed;     /* q3-lcg, msvcrt-rand */
  double draws;
  qa_double_view words;  /* glibc-random, q2-rerelease-mt19937 */
  double front;          /* glibc-random */
  double rear;           /* glibc-random */
  double index;          /* q2-rerelease-mt19937 */
  const char *module;    /* guest (also the MT19937 distribution is the
                            literal "msvc-2022-17.6") */
  qa_byte_view bytes;    /* guest */
} qa_random_state;

/** TS RandomSource interface: function table + ctx. */
typedef struct qa_random_source {
  void *ctx;
  double (*next_integer)(void *ctx);
  double (*next_unit)(void *ctx);
  qa_random_state (*checkpoint)(void *ctx);
} qa_random_source;

#endif
