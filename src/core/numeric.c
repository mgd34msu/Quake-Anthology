/* Port of src/core/numeric.ts. GPL-2.0-or-later. */
#include "qa/core/numeric.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"

/* JS ToUint32: non-finite -> 0, trunc toward zero, modulo 2^32. */
uint32_t qa_uint32(double value) {
  if (!isfinite(value)) return 0;
  double magnitude = fmod(fabs(trunc(value)), 4294967296.0);
  uint32_t bits = (uint32_t)magnitude;
  return value < 0 && bits != 0 ? (uint32_t)(0u - bits) : bits;
}

int32_t qa_int32(double value) {
  uint32_t bits = qa_uint32(value);
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}

float qa_float32(double value) { return (float)value; }

int32_t qa_qvm_float_to_int(double value) {
  float stored = (float)value;
  if ((double)stored >= -2147483648.0 && (double)stored < 2147483648.0) {
    return (int32_t)trunc((double)stored);
  }
  return INT32_MIN;
}

uint32_t qa_float32_to_bits(double value) { return qa_float32_bits(value); }
float qa_bits_to_float32(uint32_t bits) { return qa_float32_from_bits(bits); }

int32_t qa_q_rand(double seed) {
  /* Math.imul(69069, int32(seed)) + 1, wrapped in int32. */
  uint32_t product = 69069u * (uint32_t)qa_int32(seed);
  uint32_t bits = product + 1u;
  int32_t result;
  memcpy(&result, &bits, sizeof result);
  return result;
}

qa_random_step qa_q_random(double seed) {
  int32_t next = qa_q_rand(seed);
  qa_random_step step = {(double)next, (double)((uint32_t)next & 0xffffu) / 65536.0};
  return step;
}

qa_random_step qa_q_crandom(double seed) {
  qa_random_step step = qa_q_random(seed);
  step.value = 2.0 * (step.value - 0.5);
  return step;
}

const qa_numeric_profile qa_q3_binary32_profile = {
    "q3:binary32",
    {QA_ARITHMETIC_BINARY32, NULL, 0, QA_FLOATING_ROUNDING_NEAREST_EVEN, false, false},
    QA_FLOAT_TO_INT_QVM_INDEFINITE};
const qa_numeric_profile qa_q1_donor_profile = {
    "q1:donor-binary64",
    {QA_ARITHMETIC_DONOR_BINARY64, "q1-ts", 0, QA_FLOATING_ROUNDING_NEAREST_EVEN, false, false},
    QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION};
const qa_numeric_profile qa_q2_donor_profile = {
    "q2:donor-binary64",
    {QA_ARITHMETIC_DONOR_BINARY64, "q2-ts", 0, QA_FLOATING_ROUNDING_NEAREST_EVEN, false, false},
    QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION};

int32_t qa_checked_float_to_int(double value) {
  double integer = trunc(value);
  if (!isfinite(integer) || integer < -2147483648.0 || integer > 2147483647.0) {
    qa_range_error("Float-to-int conversion is outside the defined signed 32-bit range");
  }
  return (int32_t)(integer + 0.0); /* +0 normalizes -0, as in the TS */
}

/* Operations close over the rounding/conversion choice via ctx flags. */
typedef struct ops_state {
  int round_binary32;
  int checked;
} ops_state;

static double ops_round(const ops_state *state, double value) {
  return state->round_binary32 ? (double)(float)value : value;
}
static double op_store(void *ctx, double value) {
  (void)ctx;
  return (double)(float)value;
}
static double op_add(void *ctx, double l, double r) { return ops_round(ctx, l + r); }
static double op_sub(void *ctx, double l, double r) { return ops_round(ctx, l - r); }
static double op_mul(void *ctx, double l, double r) { return ops_round(ctx, l * r); }
static double op_div(void *ctx, double l, double r) { return ops_round(ctx, l / r); }
static double op_sqrt(void *ctx, double v) { return ops_round(ctx, sqrt(v)); }
static double op_to_int(void *ctx, double v) {
  return ((ops_state *)ctx)->checked ? (double)qa_checked_float_to_int(v)
                                     : (double)qa_qvm_float_to_int(v);
}
static double op_wrap_i(void *ctx, double v) { (void)ctx; return (double)qa_int32(v); }
static double op_wrap_u(void *ctx, double v) { (void)ctx; return (double)qa_uint32(v); }

typedef struct ops_entry {
  const qa_numeric_profile *profile;
  ops_state state;
  qa_numeric_operations operations;
} ops_entry;

/* Entries are individually allocated so returned pointers stay stable. */
static ops_entry **ops_cache;
static size_t ops_cache_count;
static size_t ops_cache_capacity;

const qa_numeric_operations *qa_create_numeric_operations(
    const qa_numeric_profile *profile) {
  for (size_t i = 0; i < ops_cache_count; i++) {
    if (ops_cache[i]->profile == profile) return &ops_cache[i]->operations;
  }
  int round_binary32;
  switch (profile->arithmetic.kind) {
    case QA_ARITHMETIC_BINARY32:
      round_binary32 = 1;
      break;
    case QA_ARITHMETIC_DONOR_BINARY64:
      round_binary32 = 0;
      break;
    case QA_ARITHMETIC_SSE:
      if (profile->arithmetic.rounding != QA_FLOATING_ROUNDING_NEAREST_EVEN ||
          profile->arithmetic.flush_to_zero || profile->arithmetic.denormals_are_zero) {
        qa_range_error("Numeric profile %s requires an SSE arithmetic backend", profile->id);
      }
      round_binary32 = 1;
      break;
    case QA_ARITHMETIC_X87:
    default:
      qa_range_error("Numeric profile %s requires an x87 arithmetic backend", profile->id);
  }
  if (ops_cache_count == ops_cache_capacity) {
    size_t capacity = ops_cache_capacity == 0 ? 8 : ops_cache_capacity * 2;
    ops_entry **grown = (ops_entry **)realloc(ops_cache, capacity * sizeof *grown);
    if (grown == NULL) return NULL; /* OOM has no TS counterpart */
    ops_cache = grown;
    ops_cache_capacity = capacity;
  }
  ops_entry *entry = (ops_entry *)malloc(sizeof *entry);
  if (entry == NULL) return NULL;
  ops_cache[ops_cache_count++] = entry;
  entry->profile = profile;
  entry->state.round_binary32 = round_binary32;
  entry->state.checked = profile->float_to_int == QA_FLOAT_TO_INT_CHECKED_C_TRUNCATION;
  entry->operations.ctx = &entry->state;
  entry->operations.profile = *profile;
  entry->operations.store = op_store;
  entry->operations.add = op_add;
  entry->operations.subtract = op_sub;
  entry->operations.multiply = op_mul;
  entry->operations.divide = op_div;
  entry->operations.square_root = op_sqrt;
  entry->operations.to_int32 = op_to_int;
  entry->operations.wrap_int32 = op_wrap_i;
  entry->operations.wrap_uint32 = op_wrap_u;
  return &entry->operations;
}

void qa_q3_random_init(qa_q3_random *random, int64_t seed, int64_t draws) {
  if (seed < -2147483648LL || seed > 2147483647LL) {
    qa_range_error("Q3 random seed must be a signed 32-bit integer");
  }
  if (draws < 0 || draws > QA_MAX_SAFE_INTEGER) {
    qa_range_error("Invalid random draw count");
  }
  random->seed = (int32_t)seed;
  random->draws = draws;
}

int32_t qa_q3_random_next_integer(qa_q3_random *random) {
  random->seed = qa_q_rand((double)random->seed);
  random->draws++;
  return random->seed;
}

double qa_q3_random_next_unit(qa_q3_random *random) {
  return (double)((uint32_t)qa_q3_random_next_integer(random) & 0xffffu) / 65536.0;
}

qa_random_state qa_q3_random_checkpoint(const qa_q3_random *random) {
  qa_random_state state = {0};
  state.kind = QA_RANDOM_STATE_Q3_LCG;
  state.seed = (double)random->seed;
  state.draws = (double)random->draws;
  return state;
}

void qa_q3_random_restore(qa_q3_random *random, qa_random_state state) {
  /* The TS restore constructs Q3Random(seed, draws), validating both. */
  /* Number.isInteger and the int32 range before any cast (a saved seed of
   * Infinity or 1e300 is a RangeError in TS, not a C cast). */
  if (state.seed != trunc(state.seed) || state.seed < -2147483648.0 || state.seed > 2147483647.0) {
    qa_range_error("Q3 random seed must be a signed 32-bit integer");
  }
  if (state.draws != trunc(state.draws) || fabs(state.draws) > 9007199254740991.0) {
    qa_range_error("Invalid random draw count");
  }
  qa_q3_random_init(random, (int64_t)state.seed, (int64_t)state.draws);
}

static int is_space_byte(uint8_t byte) { return byte == 32 || (byte >= 9 && byte <= 13); }

int32_t qa_native_atoi(const char *text) {
  const uint8_t *bytes = (const uint8_t *)text;
  size_t offset = 0;
  while (bytes[offset] != 0 && is_space_byte(bytes[offset])) offset++;
  int negative = 0;
  if (bytes[offset] == 43 || bytes[offset] == 45) {
    negative = bytes[offset] == 45;
    offset++;
  }
  const int64_t limit = negative ? 2147483648LL : 2147483647LL;
  int64_t magnitude = 0;
  int overflow = 0;
  while (bytes[offset] != 0) {
    uint8_t byte = bytes[offset];
    if (byte < 48 || byte > 57) break;
    int digit = byte - 48;
    if (!overflow) {
      if (magnitude > (limit - digit) / 10) {
        magnitude = limit;
        overflow = 1;
      } else {
        magnitude = magnitude * 10 + digit;
      }
    }
    offset++;
  }
  if (magnitude == 0) return 0;
  return (int32_t)(negative ? -magnitude : magnitude);
}

static uint8_t lower_ascii(uint8_t byte) {
  return byte >= 65 && byte <= 90 ? (uint8_t)(byte + 32) : byte;
}

static int matches_word(const uint8_t *bytes, size_t offset, const char *word) {
  for (size_t i = 0; word[i] != '\0'; i++) {
    uint8_t byte = bytes[offset + i];
    if (byte == 0 || lower_ascii(byte) != (uint8_t)word[i]) return 0;
  }
  return 1;
}

static int digit_value(uint8_t byte, int radix) {
  int value = -1;
  if (byte >= 48 && byte <= 57) value = byte - 48;
  else if (byte >= 65 && byte <= 70) value = byte - 65 + 10;
  else if (byte >= 97 && byte <= 102) value = byte - 97 + 10;
  return value < radix ? value : -1;
}

/* TS parseNanPayload, saturating at UINT64_MAX like the BigInt code. */
static uint64_t parse_nan_payload(const uint8_t *bytes, size_t length, size_t offset) {
  if (offset >= length || bytes[offset] != 40) return 0;
  size_t end = offset + 1;
  while (end < length) {
    uint8_t byte = bytes[end];
    int ok = (byte >= 48 && byte <= 57) || (byte >= 65 && byte <= 90) ||
             byte == 95 || (byte >= 97 && byte <= 122);
    if (!ok) break;
    end++;
  }
  if (end >= length || bytes[end] != 41 || end == offset + 1) return 0;
  size_t cursor = offset + 1;
  int radix = 10;
  if (bytes[cursor] == 48) {
    uint8_t next = cursor + 1 < length ? bytes[cursor + 1] : 0;
    uint8_t after = cursor + 2 < length ? bytes[cursor + 2] : 0;
    if ((next == 120 || next == 88) && digit_value(after, 16) >= 0) {
      radix = 16;
      cursor += 2;
    } else {
      radix = 8;
    }
  }
  uint64_t payload = 0;
  int saw_digit = 0;
  for (; cursor < end; cursor++) {
    int digit = digit_value(bytes[cursor], radix);
    if (digit < 0) return 0;
    saw_digit = 1;
    if (payload < UINT64_MAX) {
      payload = payload > (UINT64_MAX - (uint64_t)digit) / (uint64_t)radix
                    ? UINT64_MAX
                    : payload * (uint64_t)radix + (uint64_t)digit;
    }
  }
  return saw_digit ? payload : 0;
}

static double nan_with_payload(int negative, uint64_t payload) {
  uint64_t sign = negative ? 1ULL << 63 : 0;
  uint64_t fraction = (1ULL << 51) | (payload & ((1ULL << 51) - 1));
  return qa_float64_from_bits(sign | (0x7ffULL << 52) | fraction);
}

static double convert_slice(const char *text, size_t start, size_t end, int hex) {
  size_t length = end - start;
  char *slice = (char *)malloc(length + 3);
  if (slice == NULL) return 0; /* OOM has no TS counterpart */
  memcpy(slice, text + start, length);
  slice[length] = '\0';
  if (hex && strchr(slice, 'p') == NULL && strchr(slice, 'P') == NULL) {
    memcpy(slice + length, "p0", 3); /* strtod wants the binary exponent */
  }
  double value = strtod(slice, NULL);
  free(slice);
  return value;
}

/* Mirrors TS parseHex's acceptance; on success *end is the slice end. */
static int hex_slice_end(const uint8_t *bytes, size_t length, size_t offset, size_t *end) {
  if (offset >= length || bytes[offset] != 48) return 0;
  uint8_t mark = offset + 1 < length ? bytes[offset + 1] : 0;
  if (mark != 120 && mark != 88) return 0;
  size_t cursor = offset + 2;
  int saw_point = 0, saw_digit = 0;
  while (cursor < length) {
    uint8_t byte = bytes[cursor];
    if (digit_value(byte, 16) >= 0) {
      saw_digit = 1;
      cursor++;
    } else if (byte == 46 && !saw_point) {
      saw_point = 1;
      cursor++;
    } else break;
  }
  if (!saw_digit) return 0;
  size_t slice_end = cursor;
  if (cursor < length && (bytes[cursor] == 112 || bytes[cursor] == 80)) {
    size_t digits = cursor + 1;
    if (digits < length && (bytes[digits] == 43 || bytes[digits] == 45)) digits++;
    if (digits < length && bytes[digits] >= 48 && bytes[digits] <= 57) {
      slice_end = digits + 1;
      while (slice_end < length && bytes[slice_end] >= 48 && bytes[slice_end] <= 57) slice_end++;
    }
  }
  *end = slice_end;
  return 1;
}

double qa_native_atof(const char *text) {
  const uint8_t *bytes = (const uint8_t *)text;
  size_t length = strlen(text);
  size_t offset = 0;
  while (offset < length && is_space_byte(bytes[offset])) offset++;
  size_t signed_start = offset;
  int negative = 0;
  if (offset < length && (bytes[offset] == 43 || bytes[offset] == 45)) {
    negative = bytes[offset] == 45;
    offset++;
  }
  if (matches_word(bytes, offset, "inf")) return negative ? -INFINITY : INFINITY;
  if (matches_word(bytes, offset, "nan")) {
    return nan_with_payload(negative, parse_nan_payload(bytes, length, offset + 3));
  }
  size_t hex_end;
  if (hex_slice_end(bytes, length, offset, &hex_end)) {
    return convert_slice(text, signed_start, hex_end, 1);
  }
  size_t cursor = offset;
  int saw_digit = 0;
  while (cursor < length && bytes[cursor] >= 48 && bytes[cursor] <= 57) {
    saw_digit = 1;
    cursor++;
  }
  if (cursor < length && bytes[cursor] == 46) {
    cursor++;
    while (cursor < length && bytes[cursor] >= 48 && bytes[cursor] <= 57) {
      saw_digit = 1;
      cursor++;
    }
  }
  if (!saw_digit) return 0;
  if (cursor < length && (bytes[cursor] == 101 || bytes[cursor] == 69)) {
    size_t digits = cursor + 1;
    if (digits < length && (bytes[digits] == 43 || bytes[digits] == 45)) digits++;
    if (digits < length && bytes[digits] >= 48 && bytes[digits] <= 57) {
      cursor = digits + 1;
      while (cursor < length && bytes[cursor] >= 48 && bytes[cursor] <= 57) cursor++;
    }
  }
  return convert_slice(text, signed_start, cursor, 0);
}
