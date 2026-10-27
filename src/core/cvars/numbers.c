/* Port of src/core/cvars/numbers.ts. GPL-2.0-or-later. */
#include "qa/core/cvars/numbers.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/binary.h"
#include "qa/foundation/support.h"

double qa_quake_atof(const char *text) {
  size_t length = strlen(text);
  size_t offset = 0;
  double sign = 1, value = 0;
  /* charAt past the end is "" in the TS; the C NUL fails the same tests. */
  if (offset < length && text[offset] == '-') {
    sign = -1;
    offset++;
  }
  if (offset < length && text[offset] == '0' && offset + 1 < length &&
      (text[offset + 1] == 'x' || text[offset + 1] == 'X')) {
    offset += 2;
    while (offset < length) {
      int byte = (unsigned char)text[offset++];
      int digit = byte >= 48 && byte <= 57    ? byte - 48
                  : byte >= 97 && byte <= 102 ? byte - 87
                  : byte >= 65 && byte <= 70  ? byte - 55
                                              : -1;
      if (digit < 0) break;
      value = value * 16 + digit;
    }
    return value * sign;
  }
  if (offset < length && text[offset] == '\'') {
    /* charCodeAt(offset + 1) || 0: NUL and past-the-end both give 0. */
    int code = offset + 1 < length ? (unsigned char)text[offset + 1] : 0;
    return sign * code;
  }
  int64_t decimal = -1, total = 0;
  while (offset < length) {
    int byte = (unsigned char)text[offset++];
    if (byte == 46) {
      decimal = total;
      continue;
    }
    if (byte < 48 || byte > 57) break;
    value = value * 10 + byte - 48;
    total++;
  }
  if (decimal != -1) {
    while (total-- > decimal) value /= 10;
  }
  return value * sign;
}

/* Fixed 160-bit bignum for the TS BigInt scaled value (< 2^148). */
#define BIG_LIMBS 5
typedef struct big {
  uint32_t limb[BIG_LIMBS]; /* little-endian */
} big;

static void big_from_u64(big *out, uint64_t value) {
  for (int i = 0; i < BIG_LIMBS; i++) out->limb[i] = 0;
  out->limb[0] = (uint32_t)value;
  out->limb[1] = (uint32_t)(value >> 32);
}

static void big_shl(big *out, unsigned shift) {
  unsigned whole = shift / 32, bits = shift % 32;
  uint32_t source[BIG_LIMBS];
  memcpy(source, out->limb, sizeof source);
  for (int i = BIG_LIMBS - 1; i >= 0; i--) {
    uint32_t low = 0, high = 0;
    int from = i - (int)whole;
    if (from >= 0) low = source[from];
    if (bits != 0 && from - 1 >= 0) high = source[from - 1];
    out->limb[i] = bits == 0 ? low : (low << bits) | (high >> (32 - bits));
  }
}

static uint32_t big_divmod_u32(big *value, uint32_t divisor) {
  uint64_t remainder = 0;
  for (int i = BIG_LIMBS - 1; i >= 0; i--) {
    uint64_t current = (remainder << 32) | value->limb[i];
    value->limb[i] = (uint32_t)(current / divisor);
    remainder = current % divisor;
  }
  return (uint32_t)remainder;
}

static int big_is_zero(const big *value) {
  for (int i = 0; i < BIG_LIMBS; i++) {
    if (value->limb[i] != 0) return 0;
  }
  return 1;
}

char *qa_cvar_value_text(double input, bool integer_shortcut) {
  float value = (float)input; /* Math.fround */
  if (!isfinite((double)value)) {
    qa_range_error("Cvar_SetValue requires a finite float");
  }
  double as_double = (double)value;
  if (integer_shortcut && as_double >= -2147483648.0 &&
      as_double < 2147483648.0 && as_double == trunc(as_double)) {
    /* String(value); String(-0) is "0", as is the int64 conversion. */
    char buffer[32];
    snprintf(buffer, sizeof buffer, "%lld", (long long)(int64_t)as_double);
    size_t length = strlen(buffer) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL) memcpy(copy, buffer, length);
    return copy;
  }
  uint32_t bits = qa_float32_bits(as_double);
  uint32_t exponent = (bits >> 23) & 255u, fraction = bits & 0x7fffffu;
  uint64_t significand = exponent == 0 ? fraction : fraction + 0x800000u;
  int shift = exponent == 0 ? -149 : (int)exponent - 150;
  uint64_t scaled = significand * 1000000u;
  big rounded;
  if (shift >= 0) {
    big_from_u64(&rounded, scaled);
    big_shl(&rounded, (unsigned)shift);
  } else {
    /* The divisor is a power of two, so the BigInt divide is a shift. */
    unsigned down = (unsigned)-shift;
    uint64_t lower = down >= 64 ? 0 : scaled >> down;
    uint64_t result = lower;
    if (down < 64) {
      uint64_t divisor = 1ULL << down;
      uint64_t remainder = scaled - (lower << down);
      uint64_t doubled = remainder * 2;
      if (doubled > divisor || (doubled == divisor && lower % 2 != 0)) {
        result = lower + 1;
      }
    }
    /* down >= 64: remainder * 2 < 2^45 < divisor, so rounded is 0. */
    big_from_u64(&rounded, result);
  }
  uint32_t frac = big_divmod_u32(&rounded, 1000000u);
  /* Integer part in decimal, most significant group first. */
  char digits[64];
  size_t used = 0;
  if (big_is_zero(&rounded)) {
    digits[used++] = '0';
    digits[used] = '\0';
  } else {
    char reversed[64];
    size_t count = 0;
    while (!big_is_zero(&rounded)) {
      uint32_t digit = big_divmod_u32(&rounded, 10u);
      reversed[count++] = (char)('0' + digit);
    }
    for (size_t i = 0; i < count; i++) digits[i] = reversed[count - 1 - i];
    digits[count] = '\0';
    used = count;
  }
  const char *sign = bits >> 31 ? "-" : "";
  size_t total_length = strlen(sign) + used + 1 + 6 + 1;
  char *out = (char *)malloc(total_length);
  if (out == NULL) return NULL;
  snprintf(out, total_length, "%s%s.%06u", sign, digits, frac);
  return out;
}
