/*
 * Port of src/core/numeric.ts: int32/float32 wraps, QVM conversion,
 * Q_rand, numeric profiles/operations, Q3Random, and the glibc-profile
 * nativeAtoi/nativeAtof. C accommodations:
 *  - TS `number` arguments that the source immediately wraps (int32,
 *    uint32, qRand seeds) take double and apply the same ToInt32/
 *    ToUint32 conversion (truncation toward zero, modulo 2^32, NaN and
 *    infinities become 0); results the TS holds as binary32 numbers are
 *    returned as float.
 *  - createNumericOperations caches per profile *object* in a WeakMap;
 *    the C cache keys on the profile pointer and returns a retained
 *    pointer (never freed), so identity comparisons hold.
 *  - Text is a NUL-terminated byte string, so the TS charCode > 255
 *    validation is inherent. nativeAtof reproduces the TS token scan
 *    and delegates the final correctly-rounded conversion to strtod,
 *    the same glibc conversion the TS profile observes.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_NUMERIC_H
#define QA_CORE_NUMERIC_H

#include <stdint.h>

#include "qa/contracts/numeric.h"

int32_t qa_int32(double value);
uint32_t qa_uint32(double value);
float qa_float32(double value);
/* QVM CVFI4: binary32 input, INT32_MIN for NaN/out-of-range. */
int32_t qa_qvm_float_to_int(double value);
uint32_t qa_float32_to_bits(double value);
float qa_bits_to_float32(uint32_t bits);

int32_t qa_q_rand(double seed);
qa_random_step qa_q_random(double seed);
qa_random_step qa_q_crandom(double seed);

extern const qa_numeric_profile qa_q3_binary32_profile;
extern const qa_numeric_profile qa_q1_donor_profile;
extern const qa_numeric_profile qa_q2_donor_profile;

/* Aborts (RangeError) outside the defined signed 32-bit range. */
int32_t qa_checked_float_to_int(double value);

/* Retained per-profile-pointer operations; RangeError for x87 and for
 * SSE profiles that are not plain nearest-even binary32. */
const qa_numeric_operations *qa_create_numeric_operations(
    const qa_numeric_profile *profile);

typedef struct qa_q3_random {
  int32_t seed;
  int64_t draws;
} qa_q3_random;

/* Seed must fit int32 and draws must be a nonnegative safe integer
 * (TS RangeError otherwise). */
void qa_q3_random_init(qa_q3_random *random, int64_t seed, int64_t draws);
int32_t qa_q3_random_next_integer(qa_q3_random *random);
double qa_q3_random_next_unit(qa_q3_random *random);
qa_random_state qa_q3_random_checkpoint(const qa_q3_random *random);
/* TS Q3Random.restore: constructs (validating) from a checkpoint. */
void qa_q3_random_restore(qa_q3_random *random, qa_random_state state);

/* Saturating i386 glibc atoi profile over a byte string. */
int32_t qa_native_atoi(const char *text);
/* C-locale glibc atof profile (inf/nan(payload), hex, decimal). */
double qa_native_atof(const char *text);

#endif
