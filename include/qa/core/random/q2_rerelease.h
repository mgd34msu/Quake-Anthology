/*
 * Port of src/core/random/q2-rerelease.ts: MT19937 with the Microsoft
 * STL vs-2022-17.6 distribution profile. C accommodations: the TS
 * float()/integer() overloads split by argument count; the checkpoint
 * words are uint32_t[624] (the TS holds them as numbers) and the
 * distribution tag is the constant below; BigInt ranges become
 * uint64/int64 with the 2^64-wide range carried explicitly.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_RANDOM_Q2_RERELEASE_H
#define QA_CORE_RANDOM_Q2_RERELEASE_H

#include <stdint.h>

#define QA_Q2_RERELEASE_WORDS 624
#define QA_Q2_RERELEASE_DISTRIBUTION "msvc-2022-17.6"

typedef struct qa_q2_rerelease_random_state {
  uint32_t words[QA_Q2_RERELEASE_WORDS];
  int64_t index;
  int64_t draws;
} qa_q2_rerelease_random_state;

typedef struct qa_q2_rerelease_random {
  uint32_t words[QA_Q2_RERELEASE_WORDS];
  int64_t index;
  int64_t draws;
} qa_q2_rerelease_random;

void qa_q2_rerelease_random_init(qa_q2_rerelease_random *random, uint32_t seed);
/* TS default seed 5489. */
void qa_q2_rerelease_random_init_default(qa_q2_rerelease_random *random);
uint32_t qa_q2_rerelease_random_next_uint32(qa_q2_rerelease_random *random);

/* float(), float(maxExclusive), float(minInclusive, maxExclusive). */
float qa_q2_rerelease_random_float_unit(qa_q2_rerelease_random *random);
float qa_q2_rerelease_random_float_max(qa_q2_rerelease_random *random, double max_exclusive);
float qa_q2_rerelease_random_float_range(qa_q2_rerelease_random *random,
                                         double min_inclusive, double max_exclusive);
/* integer() is nextUint32; integer(maxExclusive) / (min, max) below. */
int32_t qa_q2_rerelease_random_integer_max(qa_q2_rerelease_random *random, int64_t max_exclusive);
int32_t qa_q2_rerelease_random_integer_range(qa_q2_rerelease_random *random,
                                             int64_t min_inclusive, int64_t max_exclusive);
/* uniform_int_distribution<int64_t>: both bounds inclusive. */
int64_t qa_q2_rerelease_random_integer64(qa_q2_rerelease_random *random,
                                         int64_t minimum, int64_t maximum);
/* Bounds must be safe integers (TS RangeError). */
int64_t qa_q2_rerelease_random_time_milliseconds(qa_q2_rerelease_random *random,
                                                 int64_t minimum, int64_t maximum);

void qa_q2_rerelease_random_checkpoint(const qa_q2_rerelease_random *random,
                                       qa_q2_rerelease_random_state *state);
/* TS capture() is checkpoint() under a second name. */
void qa_q2_rerelease_random_capture(const qa_q2_rerelease_random *random,
                                    qa_q2_rerelease_random_state *state);
/* Validates index/draws (TS RangeError); words/distribution inherent. */
void qa_q2_rerelease_random_restore(qa_q2_rerelease_random *random,
                                    const qa_q2_rerelease_random_state *state);

#endif
