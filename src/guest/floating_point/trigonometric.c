/* Port of src/guest/floating-point/trigonometric.ts. GPL-2.0-or-later. */
#include "qa/guest/floating_point/trigonometric.h"

#include <stdlib.h>

/* TS constants: fractionBits=256; pi is the 68-bit-pi source value scaled. */
static void big_scale(qa_bigint *out) { /* 2^256 */
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_bigint_shl(out, &one, 256);
  qa_bigint_free(&one);
}
static void big_pi(qa_bigint *out) {
  qa_bigint_set_hex(out, "3243f6a8885a308d313198a2e03707344a4093822299f31d0082efa98ec4e6c89");
}
static void big_source_pi(qa_bigint *out) { qa_bigint pi; qa_bigint_init(&pi); big_pi(&pi); qa_bigint_shr(out, &pi, 190); qa_bigint_free(&pi); }
static void big_source_pi_scaled(qa_bigint *out) { qa_bigint sp; qa_bigint_init(&sp); big_source_pi(&sp); qa_bigint_shl(out, &sp, 190); qa_bigint_free(&sp); }

static void bi_set_i(qa_bigint *v, int64_t x) { qa_bigint_set_i64(v, x); }
/* out = a / b (truncating, TS bigint /) */
static void bi_div(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) { qa_bigint_divmod(out, NULL, a, b); }

void qa_x87_trigonometric(qa_x87_trig_result *out, const qa_binary_value *value, bool cosine, qa_fp_rounding mode) {
  if (value->kind == QA_BINARY_INFINITY) {
    out->kind = QA_X87_TRIG_RESULT;
    out->result.value = qa_fp_indefinite(); out->result.flags = QA_FP_INVALID; out->result.rounded_up = false;
    return;
  }
  if (value->kind != QA_BINARY_FINITE) {
    out->kind = QA_X87_TRIG_RESULT;
    qa_fp_convert_binary(&out->result, value, qa_binary80_format, mode);
    return;
  }
  if (qa_bigint_is_zero(&value->coefficient)) {
    out->kind = QA_X87_TRIG_RESULT;
    if (cosine) out->result.value = qa_fp_from_i64(1);
    else { out->result.value = qa_fp_zero(value->sign); qa_binary_value_clone(&out->result.value, value); }
    out->result.flags = 0; out->result.rounded_up = false;
    return;
  }
  int64_t exponent = (int64_t)qa_bigint_bit_length(&value->coefficient) - 1 + value->exponent;
  if (exponent >= 63) { out->kind = QA_X87_TRIG_OUT_OF_RANGE; return; }
  int flags = QA_FP_PRECISION | (value->denormal ? QA_FP_DENORMAL_OPERAND : 0);
  qa_bigint scale; qa_bigint_init(&scale); big_scale(&scale);
  qa_bigint pi; qa_bigint_init(&pi); big_pi(&pi);
  if (exponent <= -128) {
    qa_binary_value approx = qa_fp_zero(cosine ? 0 : value->sign);
    if (cosine) {
      qa_bigint one; qa_bigint_init(&one); bi_set_i(&one, 1);
      qa_bigint_sub(&approx.coefficient, &scale, &one);
      qa_bigint_free(&one);
      approx.exponent = -256;
    } else {
      /* ((coefficient * pi) << 128) / sourcePi, exponent - 318 */
      qa_bigint t; qa_bigint_init(&t); qa_bigint_mul(&t, &value->coefficient, &pi);
      qa_bigint_shl(&t, &t, 128);
      qa_bigint sp; qa_bigint_init(&sp); big_source_pi(&sp);
      bi_div(&approx.coefficient, &t, &sp);
      qa_bigint_free(&t); qa_bigint_free(&sp);
      approx.exponent = value->exponent - 318;
    }
    out->kind = QA_X87_TRIG_RESULT;
    qa_fp_convert_binary(&out->result, &approx, qa_binary80_format, mode);
    out->result.flags |= flags;
    qa_binary_value_free(&approx);
    qa_bigint_free(&scale); qa_bigint_free(&pi);
    return;
  }
  qa_bigint sps; qa_bigint_init(&sps); big_source_pi_scaled(&sps);
  qa_bigint two_sps; qa_bigint_init(&two_sps); qa_bigint two; qa_bigint_init(&two); bi_set_i(&two, 2);
  qa_bigint_mul(&two_sps, &sps, &two); qa_bigint_free(&two);
  int64_t shift = value->exponent + 256;
  qa_bigint angle; qa_bigint_init(&angle);
  if (shift >= 0) qa_bigint_shl(&angle, &value->coefficient, shift);
  else qa_bigint_shr(&angle, &value->coefficient, -shift);
  { qa_bigint rem; qa_bigint_init(&rem); qa_bigint_divmod(NULL, &rem, &angle, &two_sps); qa_bigint_free(&angle); angle = rem; }
  if (qa_bigint_cmp(&angle, &sps) > 0) qa_bigint_sub(&angle, &angle, &two_sps);
  if (value->sign == 1) qa_bigint_neg(&angle, &angle);
  qa_bigint_mul(&angle, &angle, &pi);
  bi_div(&angle, &angle, &sps);
  qa_bigint squared; qa_bigint_init(&squared); qa_bigint_mul(&squared, &angle, &angle);
  qa_bigint scale2; qa_bigint_init(&scale2); qa_bigint_mul(&scale2, &scale, &scale);
  qa_bigint term; qa_bigint_init(&term); qa_bigint_clone(&term, cosine ? &scale : &angle);
  qa_bigint sum; qa_bigint_init(&sum); qa_bigint_clone(&sum, &term);
  for (int64_t index = 1; !qa_bigint_is_zero(&term); index++) {
    int64_t d = cosine ? (2 * index - 1) * (2 * index) : (2 * index) * (2 * index + 1);
    qa_bigint neg; qa_bigint_init(&neg); qa_bigint_neg(&neg, &term);
    qa_bigint num; qa_bigint_init(&num); qa_bigint_mul(&num, &neg, &squared);
    qa_bigint den; qa_bigint_init(&den); bi_set_i(&den, d); qa_bigint_mul(&den, &scale2, &den);
    bi_div(&term, &num, &den);
    qa_bigint_free(&neg); qa_bigint_free(&num); qa_bigint_free(&den);
    qa_bigint_add(&sum, &sum, &term);
  }
  qa_binary_value finite = qa_fp_zero(sum.sign < 0 ? 1 : 0);
  qa_bigint_clone(&finite.coefficient, &sum);
  if (finite.coefficient.sign < 0) finite.coefficient.sign = 1;
  finite.exponent = -256;
  out->kind = QA_X87_TRIG_RESULT;
  qa_fp_convert_binary(&out->result, &finite, qa_binary80_format, mode);
  out->result.flags |= flags;
  qa_binary_value_free(&finite);
  qa_bigint_free(&scale); qa_bigint_free(&pi); qa_bigint_free(&sps); qa_bigint_free(&two_sps);
  qa_bigint_free(&angle); qa_bigint_free(&squared); qa_bigint_free(&scale2);
  qa_bigint_free(&term); qa_bigint_free(&sum);
}

static void angle_result(qa_binary_result *out, qa_fp_sign sign, const qa_bigint *angle, int source_flags, qa_fp_rounding mode) {
  qa_bigint scale; qa_bigint_init(&scale); big_scale(&scale);
  qa_fp_round_rational(out, sign, angle, &scale, 0, qa_binary80_format, mode);
  out->flags |= source_flags | (qa_bigint_is_zero(angle) ? 0 : QA_FP_PRECISION);
  qa_bigint_free(&scale);
}

void qa_x87_arctangent(qa_binary_result *out, const qa_binary_value *y, const qa_binary_value *x, qa_fp_rounding mode) {
  if (y->kind == QA_BINARY_NAN || y->kind == QA_BINARY_UNSUPPORTED || x->kind == QA_BINARY_NAN || x->kind == QA_BINARY_UNSUPPORTED) {
    qa_fp_arithmetic(out, QA_FP_ADD, y, x, qa_binary80_format, mode, QA_FP_NAN_X87);
    return;
  }
  int source_flags = (y->kind == QA_BINARY_FINITE && y->denormal) || (x->kind == QA_BINARY_FINITE && x->denormal) ? QA_FP_DENORMAL_OPERAND : 0;
  qa_bigint pi; qa_bigint_init(&pi); big_pi(&pi);
  qa_bigint scale; qa_bigint_init(&scale); big_scale(&scale);
  qa_bigint four; qa_bigint_init(&four); bi_set_i(&four, 4);
  qa_bigint two; qa_bigint_init(&two); bi_set_i(&two, 2);
  qa_bigint three; qa_bigint_init(&three); bi_set_i(&three, 3);
  if (y->kind == QA_BINARY_FINITE && qa_bigint_is_zero(&y->coefficient)) {
    if (x->sign == 1) angle_result(out, y->sign, &pi, source_flags, mode);
    else { out->value = qa_fp_zero(y->sign); out->flags = source_flags; out->rounded_up = false; }
    goto done;
  }
  if (y->kind == QA_BINARY_INFINITY) {
    qa_bigint angle; qa_bigint_init(&angle);
    if (x->kind == QA_BINARY_INFINITY && x->sign == 1) { qa_bigint_mul(&angle, &pi, &three); bi_div(&angle, &angle, &four); }
    else if (x->kind == QA_BINARY_INFINITY) bi_div(&angle, &pi, &four);
    else bi_div(&angle, &pi, &two);
    angle_result(out, y->sign, &angle, source_flags, mode);
    qa_bigint_free(&angle);
    goto done;
  }
  if (x->kind == QA_BINARY_INFINITY) {
    if (x->sign == 1) angle_result(out, y->sign, &pi, source_flags, mode);
    else { qa_bigint z; qa_bigint_init(&z); angle_result(out, y->sign, &z, source_flags, mode); qa_bigint_free(&z); }
    goto done;
  }
  if (qa_bigint_is_zero(&x->coefficient)) {
    qa_bigint angle; qa_bigint_init(&angle); bi_div(&angle, &pi, &two);
    angle_result(out, y->sign, &angle, source_flags, mode);
    qa_bigint_free(&angle);
    goto done;
  }
  {
    int64_t shift = y->exponent - x->exponent;
    qa_bigint num; qa_bigint_init(&num); qa_bigint den; qa_bigint_init(&den);
    if (shift >= 0) { qa_bigint_shl(&num, &y->coefficient, shift); qa_bigint_clone(&den, &x->coefficient); }
    else { qa_bigint_clone(&num, &y->coefficient); qa_bigint_shl(&den, &x->coefficient, -shift); }
    bool tiny = false;
    if (x->sign == 0) {
      qa_bigint shifted; qa_bigint_init(&shifted); qa_bigint_shl(&shifted, &num, 128);
      tiny = qa_bigint_cmp(&shifted, &den) <= 0;
      qa_bigint_free(&shifted);
    }
    if (tiny) {
      qa_bigint one; qa_bigint_init(&one); bi_set_i(&one, 1);
      qa_bigint sm1; qa_bigint_init(&sm1); qa_bigint_sub(&sm1, &scale, &one);
      qa_bigint n2; qa_bigint_init(&n2); qa_bigint_mul(&n2, &num, &sm1);
      qa_bigint d2; qa_bigint_init(&d2); qa_bigint_mul(&d2, &den, &scale);
      qa_fp_round_rational(out, y->sign, &n2, &d2, 0, qa_binary80_format, mode);
      out->flags |= source_flags | QA_FP_PRECISION;
      qa_bigint_free(&one); qa_bigint_free(&sm1); qa_bigint_free(&n2); qa_bigint_free(&d2);
      qa_bigint_free(&num); qa_bigint_free(&den);
      goto done;
    }
    bool reciprocal = qa_bigint_cmp(&num, &den) > 0;
    qa_bigint z; qa_bigint_init(&z);
    {
      qa_bigint n2; qa_bigint_init(&n2);
      if (reciprocal) { qa_bigint_mul(&n2, &den, &scale); bi_div(&z, &n2, &num); }
      else { qa_bigint_mul(&n2, &num, &scale); bi_div(&z, &n2, &den); }
      qa_bigint_free(&n2);
    }
    qa_bigint half; qa_bigint_init(&half); bi_div(&half, &scale, &two);
    bool reduced = qa_bigint_cmp(&z, &half) > 0;
    qa_bigint_free(&half);
    if (reduced) {
      qa_bigint a; qa_bigint_init(&a); qa_bigint_sub(&a, &z, &scale); qa_bigint_mul(&a, &a, &scale);
      qa_bigint b; qa_bigint_init(&b); qa_bigint_add(&b, &z, &scale);
      bi_div(&z, &a, &b);
      qa_bigint_free(&a); qa_bigint_free(&b);
    }
    qa_bigint squared; qa_bigint_init(&squared); qa_bigint_mul(&squared, &z, &z);
    qa_bigint scale2; qa_bigint_init(&scale2); qa_bigint_mul(&scale2, &scale, &scale);
    qa_bigint power; qa_bigint_init(&power); qa_bigint_clone(&power, &z);
    qa_bigint angle; qa_bigint_init(&angle); qa_bigint_clone(&angle, &z);
    for (int64_t index = 1; !qa_bigint_is_zero(&power); index++) {
      qa_bigint neg; qa_bigint_init(&neg); qa_bigint_neg(&neg, &power);
      qa_bigint n2; qa_bigint_init(&n2); qa_bigint_mul(&n2, &neg, &squared);
      bi_div(&power, &n2, &scale2);
      qa_bigint_free(&neg); qa_bigint_free(&n2);
      qa_bigint d; qa_bigint_init(&d); bi_set_i(&d, 2 * index + 1);
      qa_bigint add; qa_bigint_init(&add); bi_div(&add, &power, &d);
      qa_bigint_add(&angle, &angle, &add);
      qa_bigint_free(&d); qa_bigint_free(&add);
    }
    if (reduced) { qa_bigint q; qa_bigint_init(&q); bi_div(&q, &pi, &four); qa_bigint_add(&angle, &angle, &q); qa_bigint_free(&q); }
    if (reciprocal) { qa_bigint h; qa_bigint_init(&h); bi_div(&h, &pi, &two); qa_bigint_sub(&angle, &h, &angle); qa_bigint_free(&h); }
    if (x->sign == 1) qa_bigint_sub(&angle, &pi, &angle);
    angle_result(out, y->sign, &angle, source_flags, mode);
    qa_bigint_free(&num); qa_bigint_free(&den); qa_bigint_free(&z);
    qa_bigint_free(&squared); qa_bigint_free(&scale2); qa_bigint_free(&power); qa_bigint_free(&angle);
  }
done:
  qa_bigint_free(&pi); qa_bigint_free(&scale); qa_bigint_free(&four); qa_bigint_free(&two); qa_bigint_free(&three);
}
