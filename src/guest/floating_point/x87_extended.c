/* Port of src/guest/floating-point/x87-extended.ts. GPL-2.0-or-later. */
#include "qa/guest/floating_point/x87_extended.h"

#include <string.h>

#include "qa/guest/floating_point/trigonometric.h"

/* Fixed-point evaluation with integer guard bits, like trigonometric.c. */
enum { BITS = 320, GUARD = 64 };

static void big_one_shl(qa_bigint *out, int64_t shift) {
  qa_bigint one;
  qa_bigint_init(&one);
  qa_bigint_set_u64(&one, 1);
  qa_bigint_shl(out, &one, shift);
  qa_bigint_free(&one);
}
static void big_scale(qa_bigint *out) { big_one_shl(out, BITS); }
static void big_div(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) { qa_bigint_divmod(out, NULL, a, b); }
static void big_div_i(qa_bigint *out, const qa_bigint *a, int64_t b) {
  qa_bigint d;
  qa_bigint_init(&d);
  qa_bigint_set_i64(&d, b);
  qa_bigint_divmod(out, NULL, a, &d);
  qa_bigint_free(&d);
}
static int64_t bit_length(const qa_bigint *value) { return (int64_t)qa_bigint_bit_length(value); }
static int64_t top_exponent(const qa_binary_value *value) {
  return bit_length(&value->coefficient) - 1 + value->exponent;
}
static bool is_nan_operand(const qa_binary_value *value) {
  return value->kind == QA_BINARY_NAN || value->kind == QA_BINARY_UNSUPPORTED;
}
static bool finite_zero(const qa_binary_value *value) {
  return value->kind == QA_BINARY_FINITE && qa_bigint_is_zero(&value->coefficient);
}
static void invalid_result(qa_binary_result *out) {
  out->value = qa_fp_indefinite();
  out->flags = QA_FP_INVALID;
  out->rounded_up = false;
}
static int denormal_flag(const qa_binary_value *value) {
  return value->kind == QA_BINARY_FINITE && value->denormal ? QA_FP_DENORMAL_OPERAND : 0;
}
static qa_fp_sign negate_sign(qa_fp_sign sign) { return sign == 0 ? 1 : 0; }
static qa_fp_sign xor_sign(qa_fp_sign a, qa_fp_sign b) { return a == b ? 0 : 1; }
static void result_of(qa_binary_result *out, const qa_binary_value *value, int flags) {
  qa_binary_value_clone(&out->value, value);
  out->flags = flags;
  out->rounded_up = false;
}
static qa_binary_value infinity_value(qa_fp_sign sign) {
  qa_binary_value value = qa_fp_zero(sign);
  value.kind = QA_BINARY_INFINITY;
  return value;
}
static void infinite_result(qa_binary_result *out, qa_fp_sign sign, int flags) {
  out->value = infinity_value(sign);
  out->flags = flags;
  out->rounded_up = false;
}
static void zero_result(qa_binary_result *out, qa_fp_sign sign, int flags) {
  out->value = qa_fp_zero(sign);
  out->flags = flags;
  out->rounded_up = false;
}
/* A finite value with an owned copy of coefficient. */
static qa_binary_value finite_value(qa_fp_sign sign, const qa_bigint *coefficient, int64_t exponent) {
  qa_binary_value value = qa_fp_zero(sign);
  qa_bigint_clone(&value.coefficient, coefficient);
  value.exponent = exponent;
  value.denormal = false;
  return value;
}
static void convert_finite(qa_binary_result *out, qa_fp_sign sign, const qa_bigint *coefficient, int64_t exponent,
                           qa_fp_rounding mode) {
  qa_binary_value value = finite_value(sign, coefficient, exponent);
  qa_fp_convert_binary(out, &value, qa_binary80_format, mode);
  qa_binary_value_free(&value);
}

/* ln 2 in scale units with guard bits, from 2 * atanh(1/3). */
static qa_bigint ln2_cache;
static bool ln2_ready;
static const qa_bigint *ln2(void) {
  if (ln2_ready) return &ln2_cache;
  qa_bigint one, power, sum, term;
  qa_bigint_init(&one);
  qa_bigint_init(&power);
  qa_bigint_init(&sum);
  qa_bigint_init(&term);
  big_one_shl(&one, BITS + GUARD);
  big_div_i(&power, &one, 3);
  qa_bigint_clone(&sum, &power);
  for (int64_t index = 1; !qa_bigint_is_zero(&power); index++) {
    big_div_i(&power, &power, 9);
    big_div_i(&term, &power, 2 * index + 1);
    qa_bigint_add(&sum, &sum, &term);
  }
  qa_bigint_init(&ln2_cache);
  qa_bigint_shl(&term, &sum, 1);
  qa_bigint_shr(&ln2_cache, &term, GUARD);
  ln2_ready = true;
  qa_bigint_free(&one);
  qa_bigint_free(&power);
  qa_bigint_free(&sum);
  qa_bigint_free(&term);
  return &ln2_cache;
}

void qa_x87_scale(qa_binary_result *out, const qa_binary_value *value, const qa_binary_value *exponent,
                  qa_fp_rounding mode) {
  memset(out, 0, sizeof *out);
  if (is_nan_operand(value) || is_nan_operand(exponent)) {
    qa_fp_arithmetic(out, QA_FP_ADD, value, exponent, qa_binary80_format, mode, QA_FP_NAN_X87);
    return;
  }
  const int flags = denormal_flag(value) | denormal_flag(exponent);
  if (exponent->kind == QA_BINARY_INFINITY) {
    if (exponent->sign == 0) {
      if (finite_zero(value)) invalid_result(out);
      else if (value->kind == QA_BINARY_INFINITY) result_of(out, value, flags);
      else infinite_result(out, value->sign, flags);
      return;
    }
    if (value->kind == QA_BINARY_INFINITY) invalid_result(out);
    else zero_result(out, value->sign, flags);
    return;
  }
  if (value->kind != QA_BINARY_FINITE || exponent->kind != QA_BINARY_FINITE || qa_bigint_is_zero(&value->coefficient)) {
    result_of(out, value, flags);
    return;
  }
  int64_t clamped = 0;
  if (!qa_bigint_is_zero(&exponent->coefficient)) {
    qa_bigint magnitude;
    qa_bigint_init(&magnitude);
    if (exponent->exponent >= 0)
      qa_bigint_shl(&magnitude, &exponent->coefficient, exponent->exponent < 64 ? exponent->exponent : 64);
    else
      qa_bigint_shr(&magnitude, &exponent->coefficient, -exponent->exponent);
    /* Anything this far out overflows or underflows regardless of the operand. */
    qa_bigint limit;
    qa_bigint_init(&limit);
    qa_bigint_set_u64(&limit, 100000);
    const int64_t count = qa_bigint_cmp(&magnitude, &limit) > 0 ? 100000 : (int64_t)qa_bigint_as_uint_n(&magnitude, 64);
    clamped = exponent->sign == 1 ? -count : count;
    qa_bigint_free(&limit);
    qa_bigint_free(&magnitude);
  }
  qa_binary_value scaled = finite_value(value->sign, &value->coefficient, value->exponent + clamped);
  qa_fp_convert_binary(out, &scaled, qa_binary80_format, mode);
  qa_binary_value_free(&scaled);
  out->flags |= flags;
}

void qa_x87_remainder(qa_x87_remainder_result *out, const qa_binary_value *dividend, const qa_binary_value *divisor,
                      bool ieee, qa_fp_rounding mode) {
  memset(out, 0, sizeof *out);
  out->complete = true;
  out->quotient = 0;
  if (is_nan_operand(dividend) || is_nan_operand(divisor)) {
    qa_fp_arithmetic(&out->result, QA_FP_ADD, dividend, divisor, qa_binary80_format, mode, QA_FP_NAN_X87);
    return;
  }
  const int flags = denormal_flag(dividend) | denormal_flag(divisor);
  if (dividend->kind == QA_BINARY_INFINITY || finite_zero(divisor)) {
    invalid_result(&out->result);
    return;
  }
  if (divisor->kind == QA_BINARY_INFINITY || dividend->kind != QA_BINARY_FINITE || divisor->kind != QA_BINARY_FINITE ||
      qa_bigint_is_zero(&dividend->coefficient)) {
    result_of(&out->result, dividend, flags);
    return;
  }
  const int64_t gap = top_exponent(dividend) - top_exponent(divisor);
  const bool partial = gap >= 64;
  const int64_t lowest = dividend->exponent < divisor->exponent ? dividend->exponent : divisor->exponent;
  qa_bigint a, b, quotient, remainder, twice, one;
  qa_bigint_init(&a);
  qa_bigint_init(&b);
  qa_bigint_init(&quotient);
  qa_bigint_init(&remainder);
  qa_bigint_init(&twice);
  qa_bigint_init(&one);
  qa_bigint_shl(&a, &dividend->coefficient, dividend->exponent - lowest);
  qa_bigint_shl(&b, &divisor->coefficient, divisor->exponent - lowest);
  if (partial) qa_bigint_shl(&b, &b, gap - 32);
  qa_bigint_divmod(&quotient, &remainder, &a, &b);
  if (ieee && !partial) {
    qa_bigint_shl(&twice, &remainder, 1);
    const int compared = qa_bigint_cmp(&twice, &b);
    if (compared > 0 || (compared == 0 && qa_bigint_test_bit(&quotient, 0))) {
      qa_bigint_set_u64(&one, 1);
      qa_bigint_add(&quotient, &quotient, &one);
      qa_bigint_sub(&remainder, &remainder, &b);
    }
  }
  const bool negative = remainder.sign < 0;
  const qa_fp_sign sign = negative ? negate_sign(dividend->sign) : dividend->sign;
  qa_bigint magnitude;
  qa_bigint_init(&magnitude);
  if (negative) qa_bigint_neg(&magnitude, &remainder);
  else qa_bigint_clone(&magnitude, &remainder);
  if (qa_bigint_is_zero(&magnitude)) zero_result(&out->result, dividend->sign, 0);
  else convert_finite(&out->result, sign, &magnitude, lowest, mode);
  out->result.flags |= flags;
  out->complete = !partial;
  /* Number(quotient & 7n): the quotient is nonnegative here. */
  out->quotient = (int)(qa_bigint_as_uint_n(&quotient, 64) & 7u);
  qa_bigint_free(&a);
  qa_bigint_free(&b);
  qa_bigint_free(&quotient);
  qa_bigint_free(&remainder);
  qa_bigint_free(&twice);
  qa_bigint_free(&one);
  qa_bigint_free(&magnitude);
}

void qa_x87_two_power_minus_one(qa_binary_result *out, const qa_binary_value *value, qa_fp_rounding mode) {
  memset(out, 0, sizeof *out);
  if (is_nan_operand(value)) {
    qa_fp_convert_binary(out, value, qa_binary80_format, mode);
    return;
  }
  if (value->kind == QA_BINARY_INFINITY) {
    if (value->sign == 0) result_of(out, value, 0);
    else {
      out->value = qa_fp_from_i64(-1);
      out->flags = 0;
      out->rounded_up = false;
    }
    return;
  }
  const int flags = QA_FP_PRECISION | denormal_flag(value);
  if (qa_bigint_is_zero(&value->coefficient)) {
    result_of(out, value, denormal_flag(value));
    return;
  }
  qa_bigint scale;
  qa_bigint_init(&scale);
  big_scale(&scale);
  if (top_exponent(value) < -300) {
    qa_bigint numerator;
    qa_bigint_init(&numerator);
    qa_bigint_mul(&numerator, &value->coefficient, ln2());
    qa_fp_round_rational(out, value->sign, &numerator, &scale, value->exponent, qa_binary80_format, mode);
    out->flags |= flags;
    qa_bigint_free(&numerator);
    qa_bigint_free(&scale);
    return;
  }
  const int64_t shift = value->exponent + BITS;
  qa_bigint fixed, y, term, sum, divisor, factor;
  qa_bigint_init(&fixed);
  qa_bigint_init(&y);
  qa_bigint_init(&term);
  qa_bigint_init(&sum);
  qa_bigint_init(&divisor);
  qa_bigint_init(&factor);
  if (shift >= 0) qa_bigint_shl(&fixed, &value->coefficient, shift);
  else qa_bigint_shr(&fixed, &value->coefficient, -shift);
  if (value->sign == 1) qa_bigint_neg(&fixed, &fixed);
  qa_bigint_mul(&y, &fixed, ln2());
  big_div(&y, &y, &scale);
  qa_bigint_clone(&term, &y);
  qa_bigint_clone(&sum, &y);
  for (int64_t index = 2; !qa_bigint_is_zero(&term); index++) {
    qa_bigint_set_i64(&factor, index);
    qa_bigint_mul(&divisor, &scale, &factor);
    qa_bigint_mul(&term, &term, &y);
    big_div(&term, &term, &divisor);
    qa_bigint_add(&sum, &sum, &term);
  }
  const bool negative = sum.sign < 0;
  if (negative) qa_bigint_neg(&sum, &sum);
  convert_finite(out, negative ? 1 : 0, &sum, -BITS, mode);
  out->flags |= flags;
  qa_bigint_free(&fixed);
  qa_bigint_free(&y);
  qa_bigint_free(&term);
  qa_bigint_free(&sum);
  qa_bigint_free(&divisor);
  qa_bigint_free(&factor);
  qa_bigint_free(&scale);
}

/* log2 of a positive finite coefficient * 2^exponent, in scale units (signed). */
static void log2_fixed(qa_bigint *out, const qa_bigint *coefficient, int64_t exponent) {
  const int64_t length = bit_length(coefficient), e = length - 1 + exponent;
  qa_bigint scale, mantissa, z, numerator, denominator, squared, power, sum, term;
  qa_bigint_init(&scale);
  qa_bigint_init(&mantissa);
  qa_bigint_init(&z);
  qa_bigint_init(&numerator);
  qa_bigint_init(&denominator);
  qa_bigint_init(&squared);
  qa_bigint_init(&power);
  qa_bigint_init(&sum);
  qa_bigint_init(&term);
  big_scale(&scale);
  if (length - 1 >= BITS) qa_bigint_shr(&mantissa, coefficient, length - 1 - BITS);
  else qa_bigint_shl(&mantissa, coefficient, BITS - (length - 1));
  qa_bigint_sub(&numerator, &mantissa, &scale);
  qa_bigint_mul(&numerator, &numerator, &scale);
  qa_bigint_add(&denominator, &mantissa, &scale);
  big_div(&z, &numerator, &denominator);
  qa_bigint_mul(&squared, &z, &z);
  big_div(&squared, &squared, &scale);
  qa_bigint_clone(&power, &z);
  qa_bigint_clone(&sum, &z);
  for (int64_t index = 1; !qa_bigint_is_zero(&power); index++) {
    qa_bigint_mul(&power, &power, &squared);
    big_div(&power, &power, &scale);
    big_div_i(&term, &power, 2 * index + 1);
    qa_bigint_add(&sum, &sum, &term);
  }
  /* BigInt(e) * scale + (2n * sum * scale) / ln2() */
  qa_bigint_shl(&term, &sum, 1);
  qa_bigint_mul(&term, &term, &scale);
  big_div(&term, &term, ln2());
  qa_bigint_set_i64(out, e);
  qa_bigint_mul(out, out, &scale);
  qa_bigint_add(out, out, &term);
  qa_bigint_free(&scale);
  qa_bigint_free(&mantissa);
  qa_bigint_free(&z);
  qa_bigint_free(&numerator);
  qa_bigint_free(&denominator);
  qa_bigint_free(&squared);
  qa_bigint_free(&power);
  qa_bigint_free(&sum);
  qa_bigint_free(&term);
}

static void log_times(qa_binary_result *out, const qa_binary_value *y, const qa_binary_value *x, bool plus_one,
                      qa_fp_rounding mode) {
  memset(out, 0, sizeof *out);
  if (is_nan_operand(y) || is_nan_operand(x)) {
    qa_fp_arithmetic(out, QA_FP_ADD, y, x, qa_binary80_format, mode, QA_FP_NAN_X87);
    return;
  }
  const int flags = denormal_flag(y) | denormal_flag(x);
  const bool y_zero = finite_zero(y);
  /* The argument of the logarithm is x (FYL2X) or 1 + x (FYL2XP1). */
  if (x->kind == QA_BINARY_INFINITY) {
    if (x->sign == 1 || y_zero) invalid_result(out);
    else infinite_result(out, y->sign, flags);
    return;
  }
  if (x->kind != QA_BINARY_FINITE) {
    invalid_result(out);
    return;
  }
  qa_bigint coefficient, scale;
  qa_bigint_init(&coefficient);
  qa_bigint_init(&scale);
  big_scale(&scale);
  qa_bigint_clone(&coefficient, &x->coefficient);
  int64_t exponent = x->exponent;
  qa_fp_sign sign = x->sign;
  if (plus_one) {
    if (qa_bigint_is_zero(&x->coefficient)) {
      if (y->kind == QA_BINARY_FINITE) zero_result(out, xor_sign(y->sign, x->sign), flags);
      else invalid_result(out);
      goto done;
    }
    if (top_exponent(x) < -200) {
      if (y->kind == QA_BINARY_INFINITY) infinite_result(out, xor_sign(y->sign, x->sign), flags);
      else if (y->kind != QA_BINARY_FINITE) invalid_result(out);
      else {
        qa_bigint reciprocal, numerator, denominator;
        qa_bigint_init(&reciprocal);
        qa_bigint_init(&numerator);
        qa_bigint_init(&denominator);
        qa_bigint_shl(&reciprocal, &scale, GUARD);
        big_div(&reciprocal, &reciprocal, ln2());
        qa_bigint_mul(&numerator, &y->coefficient, &x->coefficient);
        qa_bigint_mul(&numerator, &numerator, &reciprocal);
        big_one_shl(&denominator, GUARD);
        qa_fp_round_rational(out, xor_sign(y->sign, x->sign), &numerator, &denominator, y->exponent + x->exponent,
                             qa_binary80_format, mode);
        out->flags |= flags | QA_FP_PRECISION;
        qa_bigint_free(&reciprocal);
        qa_bigint_free(&numerator);
        qa_bigint_free(&denominator);
      }
      goto done;
    }
    const int64_t shift = exponent < 0 ? -exponent : 0;
    qa_bigint one, aligned, total;
    qa_bigint_init(&one);
    qa_bigint_init(&aligned);
    qa_bigint_init(&total);
    big_one_shl(&one, shift);
    if (exponent >= 0) qa_bigint_shl(&aligned, &x->coefficient, exponent);
    else qa_bigint_clone(&aligned, &x->coefficient);
    if (x->sign == 1) qa_bigint_sub(&total, &one, &aligned);
    else qa_bigint_add(&total, &one, &aligned);
    const bool nonpositive = total.sign <= 0;
    const bool is_zero = qa_bigint_is_zero(&total);
    if (!nonpositive) qa_bigint_clone(&coefficient, &total);
    qa_bigint_free(&one);
    qa_bigint_free(&aligned);
    qa_bigint_free(&total);
    if (nonpositive) {
      if (is_zero && !y_zero) infinite_result(out, negate_sign(y->sign), flags);
      else invalid_result(out);
      goto done;
    }
    exponent = -shift;
    sign = 0;
  } else if (qa_bigint_is_zero(&coefficient)) {
    if (y_zero) invalid_result(out);
    else infinite_result(out, negate_sign(y->sign), flags | QA_FP_ZERO_DIVIDE);
    goto done;
  } else if (sign == 1) {
    invalid_result(out);
    goto done;
  }
  {
    const int64_t length = bit_length(&coefficient);
    qa_bigint power_of_two;
    qa_bigint_init(&power_of_two);
    big_one_shl(&power_of_two, length - 1);
    const bool argument_is_one = qa_bigint_cmp(&coefficient, &power_of_two) == 0 && exponent == -(length - 1);
    qa_bigint_free(&power_of_two);
    if (y->kind == QA_BINARY_INFINITY) {
      if (argument_is_one) invalid_result(out);
      else infinite_result(out, xor_sign(exponent + length - 1 < 0 ? 1 : 0, y->sign), flags);
      goto done;
    }
    if (y->kind != QA_BINARY_FINITE) {
      invalid_result(out);
      goto done;
    }
    if (y_zero) {
      zero_result(out, y->sign, flags);
      goto done;
    }
    qa_bigint logarithm, magnitude, numerator, remainder;
    qa_bigint_init(&logarithm);
    qa_bigint_init(&magnitude);
    qa_bigint_init(&numerator);
    qa_bigint_init(&remainder);
    log2_fixed(&logarithm, &coefficient, exponent);
    if (qa_bigint_is_zero(&logarithm)) zero_result(out, y->sign, flags);
    else {
      const bool negative = logarithm.sign < 0;
      if (negative) qa_bigint_neg(&magnitude, &logarithm);
      else qa_bigint_clone(&magnitude, &logarithm);
      qa_bigint_mul(&numerator, &y->coefficient, &magnitude);
      qa_fp_round_rational(out, xor_sign(y->sign, negative ? 1 : 0), &numerator, &scale, y->exponent, qa_binary80_format,
                           mode);
      qa_bigint_divmod(NULL, &remainder, &logarithm, &scale);
      const bool exact = argument_is_one || (qa_bigint_is_zero(&remainder) && out->flags == 0);
      out->flags |= flags | (exact ? 0 : QA_FP_PRECISION);
    }
    qa_bigint_free(&logarithm);
    qa_bigint_free(&magnitude);
    qa_bigint_free(&numerator);
    qa_bigint_free(&remainder);
  }
done:
  qa_bigint_free(&coefficient);
  qa_bigint_free(&scale);
}
void qa_x87_log2(qa_binary_result *out, const qa_binary_value *y, const qa_binary_value *x, qa_fp_rounding mode) {
  log_times(out, y, x, false, mode);
}
void qa_x87_log2_plus_one(qa_binary_result *out, const qa_binary_value *y, const qa_binary_value *x,
                          qa_fp_rounding mode) {
  log_times(out, y, x, true, mode);
}

void qa_x87_extract(qa_x87_extract_result *out, const qa_binary_value *value) {
  memset(out, 0, sizeof *out);
  if (is_nan_operand(value)) {
    qa_binary_value_clone(&out->exponent, value);
    qa_binary_value_clone(&out->significand, value);
    out->flags = value->kind == QA_BINARY_NAN && !value->signaling ? 0 : QA_FP_INVALID;
    return;
  }
  if (value->kind == QA_BINARY_INFINITY) {
    out->exponent = infinity_value(0);
    qa_binary_value_clone(&out->significand, value);
    out->flags = 0;
    return;
  }
  if (qa_bigint_is_zero(&value->coefficient)) {
    out->exponent = infinity_value(1);
    qa_binary_value_clone(&out->significand, value);
    out->flags = QA_FP_ZERO_DIVIDE;
    return;
  }
  const int64_t length = bit_length(&value->coefficient);
  out->exponent = qa_fp_from_i64(top_exponent(value));
  out->significand = finite_value(value->sign, &value->coefficient, -(length - 1));
  out->flags = denormal_flag(value);
}

bool qa_x87_sine_cosine(qa_binary_result *sine, qa_binary_result *cosine, const qa_binary_value *value,
                        qa_fp_rounding mode) {
  qa_x87_trig_result sine_result, cosine_result;
  memset(&sine_result, 0, sizeof sine_result);
  memset(&cosine_result, 0, sizeof cosine_result);
  qa_x87_trigonometric(&sine_result, value, false, mode);
  qa_x87_trigonometric(&cosine_result, value, true, mode);
  if (sine_result.kind == QA_X87_TRIG_OUT_OF_RANGE || cosine_result.kind == QA_X87_TRIG_OUT_OF_RANGE) {
    if (sine_result.kind != QA_X87_TRIG_OUT_OF_RANGE) qa_binary_result_free(&sine_result.result);
    if (cosine_result.kind != QA_X87_TRIG_OUT_OF_RANGE) qa_binary_result_free(&cosine_result.result);
    return false;
  }
  *sine = sine_result.result;
  *cosine = cosine_result.result;
  return true;
}
bool qa_x87_tangent(qa_binary_result *out, const qa_binary_value *value, qa_fp_rounding mode) {
  qa_binary_result sine, cosine;
  if (!qa_x87_sine_cosine(&sine, &cosine, value, mode)) return false;
  memset(out, 0, sizeof *out);
  qa_fp_arithmetic(out, QA_FP_DIVIDE, &sine.value, &cosine.value, qa_binary80_format, mode, QA_FP_NAN_X87);
  out->flags |= sine.flags | cosine.flags;
  qa_binary_result_free(&sine);
  qa_binary_result_free(&cosine);
  return true;
}
