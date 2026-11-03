/* Port of src/core/random/q2-rerelease.ts. GPL-2.0-or-later. */
#include "qa/core/random/q2_rerelease.h"

#include <math.h>
#include <string.h>

#include "qa/foundation/support.h"

void qa_q2_rerelease_random_init(qa_q2_rerelease_random *random, uint32_t seed) {
  random->words[0] = seed;
  for (int i = 1; i < QA_Q2_RERELEASE_WORDS; i++) {
    uint32_t previous = random->words[i - 1];
    /* Math.imul wraps in 32 bits, as uint32_t multiplication does. */
    random->words[i] = 1812433253u * (previous ^ (previous >> 30)) + (uint32_t)i;
  }
  random->index = QA_Q2_RERELEASE_WORDS;
  random->draws = 0;
}

void qa_q2_rerelease_random_init_default(qa_q2_rerelease_random *random) {
  qa_q2_rerelease_random_init(random, 5489);
}

uint32_t qa_q2_rerelease_random_next_uint32(qa_q2_rerelease_random *random) {
  if (random->index == QA_Q2_RERELEASE_WORDS) {
    for (int i = 0; i < QA_Q2_RERELEASE_WORDS; i++) {
      uint32_t joined = (random->words[i] & 0x80000000u) |
                        (random->words[(i + 1) % QA_Q2_RERELEASE_WORDS] & 0x7fffffffu);
      random->words[i] = random->words[(i + 397) % QA_Q2_RERELEASE_WORDS] ^
                         (joined >> 1) ^ ((joined & 1u) == 0 ? 0u : 0x9908b0dfu);
    }
    random->index = 0;
  }
  uint32_t value = random->words[random->index++];
  value ^= value >> 11;
  value ^= (value << 7) & 0x9d2c5680u;
  value ^= (value << 15) & 0xefc60000u;
  value ^= value >> 18;
  random->draws++;
  return value;
}

static float random_float(qa_q2_rerelease_random *random, double first,
                          double second, int has_second) {
  float minimum = (float)(has_second ? first : 0.0);
  float maximum = (float)(has_second ? second : first);
  if (!isfinite((double)minimum) || !isfinite((double)maximum) || minimum > maximum) {
    qa_range_error("Invalid rerelease float range");
  }
  /* This STL revision converts the entire MT word to float first. */
  float unit = (float)((double)(float)(double)qa_q2_rerelease_random_next_uint32(random) /
                       4294967296.0);
  return (float)((double)(float)((double)unit * (double)(float)((double)maximum - (double)minimum)) +
                 (double)minimum);
}
float qa_q2_rerelease_random_float_unit(qa_q2_rerelease_random *random) {
  return random_float(random, 1.0, 0.0, 0);
}
float qa_q2_rerelease_random_float_max(qa_q2_rerelease_random *random, double max_exclusive) {
  return random_float(random, max_exclusive, 0.0, 0);
}
float qa_q2_rerelease_random_float_range(qa_q2_rerelease_random *random,
                                         double min_inclusive, double max_exclusive) {
  return random_float(random, min_inclusive, max_exclusive, 1);
}

/* 64x64 -> 128 multiply via 32-bit limbs (no __int128 under -Wpedantic). */
static void mul_u64_u128(uint64_t a, uint64_t b, uint64_t *high, uint64_t *low) {
  uint64_t a_lo = (uint32_t)a, a_hi = a >> 32;
  uint64_t b_lo = (uint32_t)b, b_hi = b >> 32;
  uint64_t p0 = a_lo * b_lo, p1 = a_lo * b_hi, p2 = a_hi * b_lo, p3 = a_hi * b_hi;
  uint64_t middle = p1 + (p0 >> 32) + (uint32_t)p2;
  *low = (middle << 32) | (uint32_t)p0;
  *high = p3 + (p2 >> 32) + (middle >> 32);
}

/*
 * TS offset(range: bigint): gathers 32 or 64 sample bits and applies
 * Lemire rejection. range_wide means the range is exactly 2^64 (only
 * from integer64's full span), where the product's high word is the
 * sample itself and the threshold is 0.
 */
static uint64_t random_offset(qa_q2_rerelease_random *random, uint64_t range, int range_wide) {
  if (!range_wide && range <= 0x100000000ULL) {
    uint64_t threshold = (0x100000000ULL - range) % range;
    for (;;) {
      uint64_t product = (uint64_t)qa_q2_rerelease_random_next_uint32(random) * range;
      if ((uint32_t)product >= threshold) return product >> 32;
    }
  }
  uint64_t threshold = range_wide ? 0 : (uint64_t)(0 - range) % range;
  for (;;) {
    /* TS: sample = next << 32n | next; the high word is drawn first. */
    uint64_t high_word = qa_q2_rerelease_random_next_uint32(random);
    uint64_t sample = (high_word << 32) | qa_q2_rerelease_random_next_uint32(random);
    if (range_wide) return sample;
    uint64_t high, low;
    mul_u64_u128(sample, range, &high, &low);
    if (low >= threshold) return high;
  }
}

static int32_t random_integer(qa_q2_rerelease_random *random, int64_t first,
                              int64_t second, int has_second) {
  int64_t minimum = has_second ? first : 0;
  int64_t maximum = has_second ? second : first;
  if (minimum < -2147483648LL || maximum > 2147483647LL) {
    qa_range_error("Invalid rerelease int32 range");
  }
  if (!has_second && maximum <= 0) return 0;
  if (minimum >= maximum) qa_range_error("Empty rerelease int32 range");
  if (minimum == maximum - 1) return (int32_t)minimum;
  uint64_t range = (uint64_t)(maximum - minimum);
  return (int32_t)(minimum + (int64_t)random_offset(random, range, 0));
}
int32_t qa_q2_rerelease_random_integer_max(qa_q2_rerelease_random *random, int64_t max_exclusive) {
  return random_integer(random, max_exclusive, 0, 0);
}
int32_t qa_q2_rerelease_random_integer_range(qa_q2_rerelease_random *random,
                                             int64_t min_inclusive, int64_t max_exclusive) {
  return random_integer(random, min_inclusive, max_exclusive, 1);
}

int64_t qa_q2_rerelease_random_integer64(qa_q2_rerelease_random *random,
                                         int64_t minimum, int64_t maximum) {
  if (minimum > maximum) qa_range_error("Invalid rerelease int64 range");
  /* range = maximum - minimum + 1 lies in [1, 2^64]. */
  uint64_t span = (uint64_t)maximum - (uint64_t)minimum;
  int range_wide = span == UINT64_MAX;
  uint64_t offset = random_offset(random, range_wide ? 0 : span + 1, range_wide);
  uint64_t bits = (uint64_t)minimum + offset;
  int64_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}

int64_t qa_q2_rerelease_random_time_milliseconds(qa_q2_rerelease_random *random,
                                                 int64_t minimum, int64_t maximum) {
  if (minimum < -QA_MAX_SAFE_INTEGER || minimum > QA_MAX_SAFE_INTEGER ||
      maximum < -QA_MAX_SAFE_INTEGER || maximum > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Rerelease millisecond bounds must be safe integers");
  }
  return qa_q2_rerelease_random_integer64(random, minimum, maximum);
}

void qa_q2_rerelease_random_checkpoint(const qa_q2_rerelease_random *random,
                                       qa_q2_rerelease_random_state *state) {
  memcpy(state->words, random->words, sizeof state->words);
  state->index = random->index;
  state->draws = random->draws;
}
void qa_q2_rerelease_random_capture(const qa_q2_rerelease_random *random,
                                    qa_q2_rerelease_random_state *state) {
  qa_q2_rerelease_random_checkpoint(random, state);
}
void qa_q2_rerelease_random_restore(qa_q2_rerelease_random *random,
                                    const qa_q2_rerelease_random_state *state) {
  if (state->index < 0 || state->index > QA_Q2_RERELEASE_WORDS ||
      state->draws < 0 || state->draws > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Invalid rerelease MT19937 checkpoint");
  }
  memcpy(random->words, state->words, sizeof random->words);
  random->index = state->index;
  random->draws = state->draws;
}
