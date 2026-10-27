/* Port of src/guest/floating-point/binary.ts. GPL-2.0-or-later. */
#include "qa/guest/floating_point/binary.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* ================= qa_bigint ================= */
static void bi_reserve(qa_bigint *v, size_t cap) {
  if (cap <= v->cap) return;
  size_t next = v->cap == 0 ? 4 : v->cap;
  while (next < cap) next *= 2;
  uint32_t *p = (uint32_t *)realloc(v->limbs, next * sizeof(uint32_t));
  if (p == NULL) qa_range_error("bigint allocation failed");
  v->limbs = p;
  v->cap = next;
}
static void bi_normalize(qa_bigint *v) {
  while (v->len > 0 && v->limbs[v->len - 1] == 0) v->len--;
  if (v->len == 0) v->sign = 0;
}
void qa_bigint_init(qa_bigint *v) { v->sign = 0; v->limbs = NULL; v->len = 0; v->cap = 0; }
void qa_bigint_free(qa_bigint *v) { free(v->limbs); qa_bigint_init(v); }
bool qa_bigint_is_zero(const qa_bigint *v) { return v->len == 0; }

void qa_bigint_clone(qa_bigint *dst, const qa_bigint *src) {
  if (dst == src) return;
  bi_reserve(dst, src->len == 0 ? 1 : src->len);
  if (src->len > 0) memcpy(dst->limbs, src->limbs, src->len * sizeof(uint32_t));
  dst->len = src->len;
  dst->sign = src->sign;
}
void qa_bigint_set_u64(qa_bigint *v, uint64_t x) {
  if (x == 0) { v->len = 0; v->sign = 0; return; }
  bi_reserve(v, 2);
  v->limbs[0] = (uint32_t)x;
  v->limbs[1] = (uint32_t)(x >> 32);
  v->len = v->limbs[1] != 0 ? 2 : 1;
  v->sign = 1;
}
void qa_bigint_set_i64(qa_bigint *v, int64_t x) {
  if (x == 0) { v->len = 0; v->sign = 0; return; }
  qa_bigint_set_u64(v, x < 0 ? (uint64_t)(-(x + 1)) + 1u : (uint64_t)x);
  v->sign = x < 0 ? -1 : 1;
}
void qa_bigint_set_hex(qa_bigint *v, const char *hex) {
  bool neg = false;
  if (*hex == '-') { neg = true; hex++; }
  if (hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex += 2;
  qa_bigint_set_u64(v, 0);
  for (; *hex != '\0'; hex++) {
    int d = *hex >= '0' && *hex <= '9' ? *hex - '0' : *hex >= 'a' && *hex <= 'f' ? *hex - 'a' + 10 : *hex - 'A' + 10;
    /* v = v*16 + d on the magnitude */
    uint64_t carry = (uint64_t)d;
    for (size_t i = 0; i < v->len; i++) {
      uint64_t cur = ((uint64_t)v->limbs[i] << 4) + carry;
      v->limbs[i] = (uint32_t)cur;
      carry = cur >> 32;
    }
    if (carry != 0) { bi_reserve(v, v->len + 1); v->limbs[v->len++] = (uint32_t)carry; }
  }
  if (v->len > 0) v->sign = neg ? -1 : 1;
}
int qa_bigint_cmp_abs(const qa_bigint *a, const qa_bigint *b) {
  if (a->len != b->len) return a->len < b->len ? -1 : 1;
  for (size_t i = a->len; i-- > 0;) {
    if (a->limbs[i] != b->limbs[i]) return a->limbs[i] < b->limbs[i] ? -1 : 1;
  }
  return 0;
}
int qa_bigint_cmp(const qa_bigint *a, const qa_bigint *b) {
  if (a->sign != b->sign) return a->sign < b->sign ? -1 : 1;
  if (a->sign == 0) return 0;
  int c = qa_bigint_cmp_abs(a, b);
  return a->sign < 0 ? -c : c;
}
size_t qa_bigint_bit_length(const qa_bigint *v) {
  if (v->len == 0) return 1; /* JS (0n).toString(2) === "0" */
  uint32_t top = v->limbs[v->len - 1];
  size_t bits = (v->len - 1) * 32u;
  while (top != 0) { bits++; top >>= 1; }
  return bits;
}
bool qa_bigint_test_bit(const qa_bigint *v, size_t bit) {
  if (v->sign >= 0) {
    size_t limb = bit / 32u;
    return limb < v->len && ((v->limbs[limb] >> (bit % 32u)) & 1u) != 0;
  }
  /* Two's-complement bit of a negative value (JS semantics). */
  qa_bigint tmp; qa_bigint_init(&tmp); qa_bigint_clone(&tmp, v); tmp.sign = 1;
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_bigint mag; qa_bigint_init(&mag); qa_bigint_sub(&mag, &tmp, &one); /* |v|-1, bits inverted */
  bool r = !qa_bigint_test_bit(&mag, bit);
  qa_bigint_free(&tmp); qa_bigint_free(&one); qa_bigint_free(&mag);
  return r;
}
uint64_t qa_bigint_as_uint_n(const qa_bigint *v, int width) {
  int limbs = width / 32;
  uint32_t word[2] = {0, 0};
  if (v->sign >= 0) {
    for (int i = 0; i < limbs && i < 2; i++) word[i] = (size_t)i < v->len ? v->limbs[i] : 0;
  } else {
    /* two's complement of the magnitude, truncated */
    uint64_t carry = 1;
    for (int i = 0; i < limbs && i < 2; i++) {
      uint32_t limb = (size_t)i < v->len ? v->limbs[i] : 0;
      uint64_t cur = (uint64_t)(~limb) + carry;
      word[i] = (uint32_t)cur;
      carry = cur >> 32;
    }
  }
  uint64_t r = (uint64_t)word[0] | ((uint64_t)word[1] << 32);
  if (width < 64) r &= width == 0 ? 0u : ((1ull << width) - 1ull);
  return r;
}
int64_t qa_bigint_as_int_n(const qa_bigint *v, int width) {
  uint64_t u = qa_bigint_as_uint_n(v, width);
  if (width > 0 && width < 64 && ((u >> (width - 1)) & 1u) != 0) return (int64_t)(u - (1ull << width));
  if (width == 64) return (int64_t)u;
  return (int64_t)u;
}
static void bi_add_abs(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) {
  size_t n = a->len > b->len ? a->len : b->len;
  bi_reserve(out, n + 1);
  uint64_t carry = 0;
  for (size_t i = 0; i < n; i++) {
    uint64_t cur = carry + (i < a->len ? a->limbs[i] : 0u) + (i < b->len ? b->limbs[i] : 0u);
    out->limbs[i] = (uint32_t)cur;
    carry = cur >> 32;
  }
  out->limbs[n] = (uint32_t)carry;
  out->len = n + 1;
  bi_normalize(out);
}
/* Requires |a| >= |b|. */
static void bi_sub_abs(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) {
  bi_reserve(out, a->len == 0 ? 1 : a->len);
  int64_t borrow = 0;
  for (size_t i = 0; i < a->len; i++) {
    int64_t cur = (int64_t)a->limbs[i] - (i < b->len ? (int64_t)b->limbs[i] : 0) - borrow;
    if (cur < 0) { cur += (int64_t)1 << 32; borrow = 1; } else borrow = 0;
    out->limbs[i] = (uint32_t)cur;
  }
  out->len = a->len;
  bi_normalize(out);
}
void qa_bigint_neg(qa_bigint *out, const qa_bigint *a) {
  qa_bigint_clone(out, a);
  out->sign = -out->sign;
}
void qa_bigint_add(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) {
  qa_bigint r; qa_bigint_init(&r);
  if (a->sign == 0) qa_bigint_clone(&r, b);
  else if (b->sign == 0) qa_bigint_clone(&r, a);
  else if (a->sign == b->sign) { bi_add_abs(&r, a, b); r.sign = r.len ? a->sign : 0; }
  else {
    int c = qa_bigint_cmp_abs(a, b);
    if (c == 0) { r.len = 0; r.sign = 0; }
    else if (c > 0) { bi_sub_abs(&r, a, b); r.sign = r.len ? a->sign : 0; }
    else { bi_sub_abs(&r, b, a); r.sign = r.len ? b->sign : 0; }
  }
  qa_bigint_free(out);
  *out = r;
}
void qa_bigint_sub(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) {
  qa_bigint nb; qa_bigint_init(&nb); qa_bigint_neg(&nb, b);
  qa_bigint_add(out, a, &nb);
  qa_bigint_free(&nb);
}
void qa_bigint_mul(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) {
  qa_bigint r; qa_bigint_init(&r);
  if (a->len != 0 && b->len != 0) {
    bi_reserve(&r, a->len + b->len);
    for (size_t i = 0; i < a->len + b->len; i++) r.limbs[i] = 0;
    r.len = a->len + b->len;
    for (size_t i = 0; i < a->len; i++) {
      uint64_t carry = 0;
      for (size_t j = 0; j < b->len; j++) {
        uint64_t cur = (uint64_t)a->limbs[i] * b->limbs[j] + r.limbs[i + j] + carry;
        r.limbs[i + j] = (uint32_t)cur;
        carry = cur >> 32;
      }
      r.limbs[i + b->len] = (uint32_t)carry;
    }
    r.sign = a->sign * b->sign;
    bi_normalize(&r);
  }
  qa_bigint_free(out);
  *out = r;
}
/* Magnitude long division over bits (sizes here are modest). */
static bool bi_get_bit(const qa_bigint *v, size_t bit) {
  size_t limb = bit / 32u;
  return limb < v->len && ((v->limbs[limb] >> (bit % 32u)) & 1u) != 0;
}
static void bi_set_bit(qa_bigint *v, size_t bit) {
  size_t limb = bit / 32u;
  bi_reserve(v, limb + 1);
  if (v->len < limb + 1) { for (size_t i = v->len; i <= limb; i++) v->limbs[i] = 0; v->len = limb + 1; }
  v->limbs[limb] |= 1u << (bit % 32u);
}
void qa_bigint_divmod(qa_bigint *q, qa_bigint *r, const qa_bigint *a, const qa_bigint *b) {
  if (b->len == 0) qa_range_error("Division by zero");
  qa_bigint quot; qa_bigint_init(&quot);
  qa_bigint rem; qa_bigint_init(&rem);
  if (qa_bigint_cmp_abs(a, b) < 0) {
    qa_bigint_clone(&rem, a);
  } else {
    size_t nbits = qa_bigint_bit_length(a);
    qa_bigint divisor; qa_bigint_init(&divisor); qa_bigint_clone(&divisor, b); divisor.sign = 1;
    for (size_t i = nbits; i-- > 0;) {
      /* rem = (rem << 1) | bit_i(a) */
      qa_bigint shifted; qa_bigint_init(&shifted);
      qa_bigint_shl(&shifted, &rem, 1);
      qa_bigint_free(&rem); rem = shifted;
      if (bi_get_bit(a, i)) bi_set_bit(&rem, 0);
      if (rem.len > 0) rem.sign = 1;
      if (qa_bigint_cmp_abs(&rem, &divisor) >= 0) {
        qa_bigint next; qa_bigint_init(&next); bi_sub_abs(&next, &rem, &divisor);
        next.sign = next.len ? 1 : 0;
        qa_bigint_free(&rem); rem = next;
        bi_set_bit(&quot, i);
      }
    }
    qa_bigint_free(&divisor);
    if (quot.len > 0) quot.sign = a->sign * b->sign;
    if (rem.len > 0) rem.sign = a->sign;
  }
  if (q != NULL) { qa_bigint_free(q); *q = quot; } else qa_bigint_free(&quot);
  if (r != NULL) { qa_bigint_free(r); *r = rem; } else qa_bigint_free(&rem);
}
void qa_bigint_shl(qa_bigint *out, const qa_bigint *a, int64_t shift) {
  if (shift < 0) { qa_bigint_shr(out, a, -shift); return; }
  qa_bigint r; qa_bigint_init(&r);
  if (a->len != 0) {
    size_t limb_shift = (size_t)shift / 32u;
    unsigned bit_shift = (unsigned)shift % 32u;
    bi_reserve(&r, a->len + limb_shift + 1);
    for (size_t i = 0; i < a->len + limb_shift + 1; i++) r.limbs[i] = 0;
    r.len = a->len + limb_shift + 1;
    for (size_t i = 0; i < a->len; i++) {
      uint64_t cur = (uint64_t)a->limbs[i] << bit_shift;
      r.limbs[i + limb_shift] |= (uint32_t)cur;
      r.limbs[i + limb_shift + 1] |= (uint32_t)(cur >> 32);
    }
    r.sign = a->sign;
    bi_normalize(&r);
  }
  qa_bigint_free(out);
  *out = r;
}
static void bi_shr_abs(qa_bigint *out, const qa_bigint *a, size_t shift) {
  qa_bigint r; qa_bigint_init(&r);
  size_t limb_shift = shift / 32u;
  unsigned bit_shift = (unsigned)shift % 32u;
  if (a->len > limb_shift) {
    size_t n = a->len - limb_shift;
    bi_reserve(&r, n);
    for (size_t i = 0; i < n; i++) {
      uint64_t lo = (uint64_t)a->limbs[i + limb_shift] >> bit_shift;
      uint64_t hi = bit_shift != 0 && i + limb_shift + 1 < a->len
                        ? ((uint64_t)a->limbs[i + limb_shift + 1] << (32u - bit_shift)) : 0;
      r.limbs[i] = (uint32_t)(lo | hi);
    }
    r.len = n;
    r.sign = a->sign;
    bi_normalize(&r);
  }
  qa_bigint_free(out);
  *out = r;
}
void qa_bigint_shr(qa_bigint *out, const qa_bigint *a, int64_t shift) {
  if (shift < 0) { qa_bigint_shl(out, a, -shift); return; }
  if (a->sign >= 0) { bi_shr_abs(out, a, (size_t)shift); return; }
  /* JS >> is floor division: -((-a + 2^shift - 1) >> shift). */
  qa_bigint mag; qa_bigint_init(&mag); qa_bigint_clone(&mag, a); mag.sign = 1;
  qa_bigint bump; qa_bigint_init(&bump); qa_bigint_set_u64(&bump, 1);
  qa_bigint tmp; qa_bigint_init(&tmp); qa_bigint_shl(&tmp, &bump, shift);
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_bigint adj; qa_bigint_init(&adj); qa_bigint_sub(&adj, &tmp, &one);
  qa_bigint sum; qa_bigint_init(&sum); qa_bigint_add(&sum, &mag, &adj);
  qa_bigint res; qa_bigint_init(&res); bi_shr_abs(&res, &sum, (size_t)shift);
  if (res.len > 0) res.sign = -1;
  qa_bigint_free(out); *out = res;
  qa_bigint_free(&mag); qa_bigint_free(&bump); qa_bigint_free(&tmp);
  qa_bigint_free(&one); qa_bigint_free(&adj); qa_bigint_free(&sum);
}
/* Bitwise ops via fixed-width two's-complement expansion (JS semantics). */
static void bi_bitwise(qa_bigint *out, const qa_bigint *a, const qa_bigint *b, bool is_and) {
  size_t width = qa_bigint_bit_length(a);
  size_t wb = qa_bigint_bit_length(b);
  if (wb > width) width = wb;
  width += 2;
  size_t limbs = (width + 31u) / 32u;
  qa_bigint r; qa_bigint_init(&r); bi_reserve(&r, limbs);
  for (size_t i = 0; i < limbs; i++) r.limbs[i] = 0;
  r.len = limbs;
  for (size_t bit = 0; bit < width; bit++) {
    bool ba = qa_bigint_test_bit(a, bit), bb = qa_bigint_test_bit(b, bit);
    if (is_and ? (ba && bb) : (ba || bb)) bi_set_bit(&r, bit);
  }
  bool neg = qa_bigint_test_bit(&r, width - 1);
  r.sign = 1;
  bi_normalize(&r);
  if (neg) {
    /* value = r - 2^width */
    qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
    qa_bigint top; qa_bigint_init(&top); qa_bigint_shl(&top, &one, (int64_t)width);
    qa_bigint diff; qa_bigint_init(&diff); qa_bigint_sub(&diff, &r, &top);
    qa_bigint_free(&r); r = diff;
    qa_bigint_free(&one); qa_bigint_free(&top);
  }
  qa_bigint_free(out);
  *out = r;
}
void qa_bigint_and(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) { bi_bitwise(out, a, b, true); }
void qa_bigint_or(qa_bigint *out, const qa_bigint *a, const qa_bigint *b) { bi_bitwise(out, a, b, false); }
void qa_bigint_pow_u32(qa_bigint *out, uint32_t base, uint32_t exp) {
  qa_bigint r; qa_bigint_init(&r); qa_bigint_set_u64(&r, 1);
  qa_bigint b; qa_bigint_init(&b); qa_bigint_set_u64(&b, base);
  while (exp != 0) {
    if ((exp & 1u) != 0) qa_bigint_mul(&r, &r, &b);
    exp >>= 1;
    if (exp != 0) qa_bigint_mul(&b, &b, &b);
  }
  qa_bigint_free(&b);
  qa_bigint_free(out);
  *out = r;
}
char *qa_bigint_to_string(const qa_bigint *v, int base) {
  if (v->len == 0) { char *s = (char *)malloc(2); if (s) { s[0] = '0'; s[1] = 0; } return s; }
  if (base == 16) {
    size_t cap = v->len * 8u + 2u;
    char *s = (char *)malloc(cap);
    if (s == NULL) return NULL;
    size_t pos = 0;
    if (v->sign < 0) s[pos++] = '-';
    bool started = false;
    for (size_t i = v->len; i-- > 0;) {
      for (int nib = 7; nib >= 0; nib--) {
        unsigned d = (v->limbs[i] >> (nib * 4)) & 15u;
        if (d != 0 || started) { s[pos++] = "0123456789abcdef"[d]; started = true; }
      }
    }
    if (!started) s[pos++] = '0';
    s[pos] = 0;
    return s;
  }
  /* decimal via repeated division by 10^9 */
  qa_bigint mag; qa_bigint_init(&mag); qa_bigint_clone(&mag, v); mag.sign = 1;
  qa_bigint chunk; qa_bigint_init(&chunk); qa_bigint_set_u64(&chunk, 1000000000u);
  size_t cap = qa_bigint_bit_length(v) + 16u;
  char *rev = (char *)malloc(cap);
  if (rev == NULL) { qa_bigint_free(&mag); qa_bigint_free(&chunk); return NULL; }
  size_t pos = 0;
  while (mag.len > 0) {
    qa_bigint qq; qa_bigint_init(&qq);
    qa_bigint rr; qa_bigint_init(&rr);
    qa_bigint_divmod(&qq, &rr, &mag, &chunk);
    uint32_t part = (uint32_t)qa_bigint_as_uint_n(&rr, 32);
    qa_bigint_free(&mag); mag = qq;
    qa_bigint_free(&rr);
    for (int k = 0; k < 9; k++) {
      if (pos + 10 >= cap) { cap *= 2; rev = (char *)realloc(rev, cap); if (!rev) { qa_bigint_free(&mag); qa_bigint_free(&chunk); return NULL; } }
      rev[pos++] = (char)('0' + (int)(part % 10u));
      part /= 10u;
    }
  }
  qa_bigint_free(&mag); qa_bigint_free(&chunk);
  while (pos > 1 && rev[pos - 1] == '0') pos--;
  char *s = (char *)malloc(pos + 2u);
  if (s == NULL) { free(rev); return NULL; }
  size_t o = 0;
  if (v->sign < 0) s[o++] = '-';
  for (size_t i = pos; i-- > 0;) s[o++] = rev[i];
  s[o] = 0;
  free(rev);
  return s;
}

/* ================= binary.ts ================= */
const qa_binary_format qa_binary32_format = {24, -126, 127};
const qa_binary_format qa_binary64_format = {53, -1022, 1023};
const qa_binary_format qa_binary80_format = {64, -16382, 16383};

const char *qa_fp_rounding_name(qa_fp_rounding m) {
  switch (m) { case QA_FP_NEAREST: return "nearest"; case QA_FP_DOWN: return "down"; case QA_FP_UP: return "up"; default: return "zero"; }
}
const char *qa_binary_kind_name(qa_binary_kind k) {
  switch (k) { case QA_BINARY_FINITE: return "finite"; case QA_BINARY_INFINITY: return "infinity"; case QA_BINARY_NAN: return "nan"; default: return "unsupported"; }
}
const char *qa_fp_comparison_name(qa_fp_comparison c) {
  switch (c) { case QA_FP_LESS: return "less"; case QA_FP_EQUAL: return "equal"; case QA_FP_GREATER: return "greater"; default: return "unordered"; }
}
const char *qa_numeric_prefix_name(qa_numeric_prefix p) {
  switch (p) { case QA_PREFIX_NONE: return "none"; case QA_PREFIX_66: return "66"; case QA_PREFIX_F2: return "f2"; default: return "f3"; }
}
qa_fp_rounding qa_fp_rounding_from_bits(int bits) {
  switch (bits & 3) { case 0: return QA_FP_NEAREST; case 1: return QA_FP_DOWN; case 2: return QA_FP_UP; default: return QA_FP_ZERO; }
}
qa_binary_format qa_fp_format_for(int width) { return width == 32 ? qa_binary32_format : width == 64 ? qa_binary64_format : qa_binary80_format; }

static qa_binary_value value_init(qa_binary_kind kind, qa_fp_sign sign) {
  qa_binary_value v; v.kind = kind; v.sign = sign; v.exponent = 0; v.denormal = false; v.signaling = false;
  qa_bigint_init(&v.coefficient); qa_bigint_init(&v.payload);
  return v;
}
void qa_binary_value_free(qa_binary_value *v) { qa_bigint_free(&v->coefficient); qa_bigint_free(&v->payload); }
void qa_binary_result_free(qa_binary_result *r) { qa_binary_value_free(&r->value); }
void qa_binary_value_clone(qa_binary_value *dst, const qa_binary_value *src) {
  dst->kind = src->kind; dst->sign = src->sign; dst->exponent = src->exponent;
  dst->denormal = src->denormal; dst->signaling = src->signaling;
  qa_bigint_clone(&dst->coefficient, &src->coefficient);
  qa_bigint_clone(&dst->payload, &src->payload);
}
qa_binary_value qa_fp_zero(qa_fp_sign sign) { return value_init(QA_BINARY_FINITE, sign); }
qa_binary_value qa_fp_indefinite(void) {
  qa_binary_value v = value_init(QA_BINARY_NAN, 1);
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_bigint_shl(&v.payload, &one, 62);
  qa_bigint_free(&one);
  return v;
}
qa_binary_value qa_fp_from_integer(const qa_bigint *value) {
  qa_binary_value v = value_init(QA_BINARY_FINITE, value->sign < 0 ? 1 : 0);
  qa_bigint_clone(&v.coefficient, value);
  if (v.coefficient.sign < 0) v.coefficient.sign = 1;
  return v;
}
qa_binary_value qa_fp_from_i64(int64_t x) {
  qa_bigint b; qa_bigint_init(&b); qa_bigint_set_i64(&b, x);
  qa_binary_value v = qa_fp_from_integer(&b);
  qa_bigint_free(&b);
  return v;
}
qa_binary_value qa_fp_negate(const qa_binary_value *value) {
  qa_binary_value v = value_init(value->kind, value->sign == 0 ? 1 : 0);
  v.exponent = value->exponent; v.denormal = value->denormal; v.signaling = value->signaling;
  qa_bigint_clone(&v.coefficient, &value->coefficient);
  qa_bigint_clone(&v.payload, &value->payload);
  return v;
}
qa_binary_value qa_fp_quiet(const qa_binary_value *value) {
  qa_binary_value v = qa_fp_negate(value); v.sign = value->sign;
  if (value->kind == QA_BINARY_NAN) {
    v.signaling = false;
    qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
    qa_bigint bit; qa_bigint_init(&bit); qa_bigint_shl(&bit, &one, 62);
    qa_bigint_or(&v.payload, &v.payload, &bit);
    qa_bigint_free(&one); qa_bigint_free(&bit);
  }
  return v;
}
void qa_fp_read_bits(qa_bigint *out, const uint8_t *bytes, size_t len) {
  qa_bigint_set_u64(out, 0);
  for (size_t i = len; i-- > 0;) {
    qa_bigint_shl(out, out, 8);
    qa_bigint b; qa_bigint_init(&b); qa_bigint_set_u64(&b, bytes[i]);
    qa_bigint_or(out, out, &b);
    qa_bigint_free(&b);
  }
}
void qa_fp_write_bits(uint8_t *out, const qa_bigint *value, size_t byte_length) {
  for (size_t i = 0; i < byte_length; i++) {
    unsigned byte = 0;
    for (int k = 0; k < 8; k++) if (qa_bigint_test_bit(value, i * 8u + (size_t)k)) byte |= 1u << k;
    out[i] = (uint8_t)byte;
  }
}
qa_binary_value qa_fp_decode_binary32(uint32_t bits) {
  qa_fp_sign sign = (bits >> 31) == 0 ? 0 : 1;
  uint32_t exponent = (bits >> 23) & 255u, fraction = bits & 0x7fffffu;
  if (exponent == 255u) {
    if (fraction == 0) return value_init(QA_BINARY_INFINITY, sign);
    qa_binary_value v = value_init(QA_BINARY_NAN, sign);
    qa_bigint f; qa_bigint_init(&f); qa_bigint_set_u64(&f, fraction);
    qa_bigint_shl(&v.payload, &f, 40);
    qa_bigint_free(&f);
    v.signaling = (fraction & 0x400000u) == 0;
    return v;
  }
  qa_binary_value v = value_init(QA_BINARY_FINITE, sign);
  qa_bigint_set_u64(&v.coefficient, exponent == 0 ? fraction : fraction | 0x800000u);
  v.exponent = (int64_t)(exponent == 0 ? 1 : (int)exponent) - 150;
  v.denormal = exponent == 0 && fraction != 0;
  return v;
}
static void bi_pow2(qa_bigint *out, int64_t n) {
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_bigint_shl(out, &one, n);
  qa_bigint_free(&one);
}
qa_binary_value qa_fp_decode_binary(const qa_bigint *bits, int width) {
  int fraction_bits = width == 32 ? 23 : width == 64 ? 52 : 63;
  int storage_bits = width == 80 ? 64 : fraction_bits;
  int exponent_bits = width == 32 ? 8 : width == 64 ? 11 : 15;
  int exponent_mask = (1 << exponent_bits) - 1;
  int bias = (1 << (exponent_bits - 1)) - 1;
  qa_fp_sign sign = qa_bigint_test_bit(bits, (size_t)(width - 1)) ? 1 : 0;
  qa_bigint tmp; qa_bigint_init(&tmp); qa_bigint_shr(&tmp, bits, storage_bits);
  int exponent = (int)qa_bigint_as_uint_n(&tmp, 32) & exponent_mask;
  qa_bigint_free(&tmp);
  qa_bigint fmask; qa_bigint_init(&fmask); bi_pow2(&fmask, fraction_bits);
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_bigint_sub(&fmask, &fmask, &one);
  qa_bigint fraction; qa_bigint_init(&fraction); qa_bigint_and(&fraction, bits, &fmask);
  qa_bigint_free(&fmask);
  int integer_bit;
  if (width == 80) integer_bit = qa_bigint_test_bit(bits, 63) ? 1 : 0;
  else integer_bit = exponent == 0 ? 0 : 1;
  if (width == 80 && exponent != 0 && integer_bit == 0) { qa_bigint_free(&fraction); qa_bigint_free(&one); return value_init(QA_BINARY_UNSUPPORTED, sign); }
  if (exponent == exponent_mask) {
    if (qa_bigint_is_zero(&fraction)) { qa_bigint_free(&fraction); qa_bigint_free(&one); return value_init(QA_BINARY_INFINITY, sign); }
    qa_binary_value v = value_init(QA_BINARY_NAN, sign);
    qa_bigint_shl(&v.payload, &fraction, 63 - fraction_bits);
    v.signaling = !qa_bigint_test_bit(&v.payload, 62);
    qa_bigint_free(&fraction); qa_bigint_free(&one);
    return v;
  }
  qa_binary_value v = value_init(QA_BINARY_FINITE, sign);
  qa_bigint ib; qa_bigint_init(&ib); qa_bigint_set_u64(&ib, (uint64_t)integer_bit);
  qa_bigint shifted; qa_bigint_init(&shifted); qa_bigint_shl(&shifted, &ib, fraction_bits);
  qa_bigint_or(&v.coefficient, &shifted, &fraction);
  qa_bigint_free(&ib); qa_bigint_free(&shifted); qa_bigint_free(&fraction); qa_bigint_free(&one);
  v.exponent = (int64_t)(exponent == 0 ? 1 : exponent) - bias - fraction_bits;
  v.denormal = exponent == 0 && !qa_bigint_is_zero(&v.coefficient) && integer_bit == 0;
  return v;
}
static int64_t bi_length(const qa_bigint *value) { return (int64_t)qa_bigint_bit_length(value); }
static int compare_scaled(const qa_bigint *left, const qa_bigint *right, int64_t shift) {
  qa_bigint a; qa_bigint_init(&a); qa_bigint b; qa_bigint_init(&b);
  if (shift >= 0) { qa_bigint_shl(&a, left, shift); qa_bigint_clone(&b, right); }
  else { qa_bigint_clone(&a, left); qa_bigint_shl(&b, right, -shift); }
  int c = qa_bigint_cmp(&a, &b);
  qa_bigint_free(&a); qa_bigint_free(&b);
  return c;
}
static int64_t ratio_exponent(const qa_bigint *num, const qa_bigint *den) {
  int64_t e = bi_length(num) - bi_length(den);
  if (compare_scaled(num, den, -e) < 0) e--;
  return e;
}
typedef struct { qa_bigint quotient; bool inexact; bool up; } rounded_quotient_t;
static void rounded_quotient(rounded_quotient_t *out, const qa_bigint *num, const qa_bigint *den, qa_fp_sign sign, qa_fp_rounding mode) {
  qa_bigint_init(&out->quotient);
  qa_bigint rem; qa_bigint_init(&rem);
  qa_bigint_divmod(&out->quotient, &rem, num, den);
  out->inexact = !qa_bigint_is_zero(&rem);
  bool up = false;
  if (out->inexact) {
    if (mode == QA_FP_NEAREST) {
      qa_bigint two; qa_bigint_init(&two); qa_bigint_set_u64(&two, 2);
      qa_bigint dbl; qa_bigint_init(&dbl); qa_bigint_mul(&dbl, &rem, &two);
      int c = qa_bigint_cmp(&dbl, den);
      up = c > 0 || (c == 0 && qa_bigint_test_bit(&out->quotient, 0));
      qa_bigint_free(&two); qa_bigint_free(&dbl);
    } else if (mode == QA_FP_UP) up = sign == 0;
    else if (mode == QA_FP_DOWN) up = sign == 1;
  }
  out->up = up;
  if (up) { qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1); qa_bigint_add(&out->quotient, &out->quotient, &one); qa_bigint_free(&one); }
  qa_bigint_free(&rem);
}
void qa_fp_round_rational(qa_binary_result *out, qa_fp_sign sign, const qa_bigint *num, const qa_bigint *den,
                          int64_t exponent, qa_binary_format format, qa_fp_rounding mode) {
  if (qa_bigint_is_zero(num)) { out->value = qa_fp_zero(sign); out->flags = 0; out->rounded_up = false; return; }
  int64_t top = ratio_exponent(num, den) + exponent;
  int64_t q1 = top - format.precision + 1, q2 = (int64_t)format.minimum_exponent - format.precision + 1;
  int64_t quantum = q1 > q2 ? q1 : q2;
  int64_t shift = exponent - quantum;
  qa_bigint n2; qa_bigint_init(&n2); qa_bigint d2; qa_bigint_init(&d2);
  if (shift >= 0) { qa_bigint_shl(&n2, num, shift); qa_bigint_clone(&d2, den); }
  else { qa_bigint_clone(&n2, num); qa_bigint_shl(&d2, den, -shift); }
  rounded_quotient_t rq; rounded_quotient(&rq, &n2, &d2, sign, mode);
  qa_bigint_free(&n2); qa_bigint_free(&d2);
  int64_t result_top = bi_length(&rq.quotient) - 1 + quantum;
  if (result_top > format.maximum_exponent) {
    bool infinite = mode == QA_FP_NEAREST || (mode == QA_FP_UP && sign == 0) || (mode == QA_FP_DOWN && sign == 1);
    if (infinite) out->value = value_init(QA_BINARY_INFINITY, sign);
    else {
      out->value = value_init(QA_BINARY_FINITE, sign);
      bi_pow2(&out->value.coefficient, format.precision);
      qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
      qa_bigint_sub(&out->value.coefficient, &out->value.coefficient, &one);
      qa_bigint_free(&one);
      out->value.exponent = (int64_t)format.maximum_exponent - format.precision + 1;
    }
    out->flags = QA_FP_OVERFLOW | QA_FP_PRECISION;
    out->rounded_up = infinite;
    qa_bigint_free(&rq.quotient);
    return;
  }
  bool tiny = result_top < format.minimum_exponent;
  out->value = value_init(QA_BINARY_FINITE, sign);
  qa_bigint_clone(&out->value.coefficient, &rq.quotient);
  out->value.exponent = quantum;
  out->value.denormal = tiny && !qa_bigint_is_zero(&rq.quotient);
  out->flags = rq.inexact ? QA_FP_PRECISION | (tiny ? QA_FP_UNDERFLOW : 0) : 0;
  out->rounded_up = rq.up;
  qa_bigint_free(&rq.quotient);
}
void qa_fp_convert_binary(qa_binary_result *out, const qa_binary_value *value, qa_binary_format format, qa_fp_rounding mode) {
  if (value->kind == QA_BINARY_UNSUPPORTED) { out->value = qa_fp_indefinite(); out->flags = QA_FP_INVALID; out->rounded_up = false; return; }
  if (value->kind == QA_BINARY_NAN) { out->value = qa_fp_quiet(value); out->flags = value->signaling ? QA_FP_INVALID : 0; out->rounded_up = false; return; }
  if (value->kind == QA_BINARY_INFINITY) { out->value = qa_fp_negate(value); out->value.sign = value->sign; out->flags = 0; out->rounded_up = false; return; }
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_fp_round_rational(out, value->sign, &value->coefficient, &one, value->exponent, format, mode);
  qa_bigint_free(&one);
}
void qa_fp_encode_binary(qa_bigint *out, const qa_binary_value *value, int width) {
  qa_binary_format format = qa_fp_format_for(width);
  int fraction_bits = width == 32 ? 23 : width == 64 ? 52 : 63;
  int storage_bits = width == 80 ? 64 : fraction_bits;
  int exponent_mask = width == 32 ? 255 : width == 64 ? 2047 : 32767;
  qa_bigint sign_bit; qa_bigint_init(&sign_bit);
  { qa_bigint s; qa_bigint_init(&s); qa_bigint_set_u64(&s, (uint64_t)value->sign); qa_bigint_shl(&sign_bit, &s, width - 1); qa_bigint_free(&s); }
  qa_bigint explicit_bit; qa_bigint_init(&explicit_bit);
  if (width == 80) bi_pow2(&explicit_bit, 63);
  if (value->kind == QA_BINARY_UNSUPPORTED) {
    qa_binary_value ind = qa_fp_indefinite();
    qa_fp_encode_binary(out, &ind, width);
    qa_binary_value_free(&ind); qa_bigint_free(&sign_bit); qa_bigint_free(&explicit_bit);
    return;
  }
  if (value->kind == QA_BINARY_INFINITY || value->kind == QA_BINARY_NAN) {
    qa_bigint e; qa_bigint_init(&e); qa_bigint_set_u64(&e, (uint64_t)exponent_mask);
    qa_bigint es; qa_bigint_init(&es); qa_bigint_shl(&es, &e, storage_bits);
    qa_bigint_or(out, &sign_bit, &es); qa_bigint_or(out, out, &explicit_bit);
    qa_bigint_free(&e); qa_bigint_free(&es);
    if (value->kind == QA_BINARY_NAN) {
      qa_bigint payload; qa_bigint_init(&payload); qa_bigint_shr(&payload, &value->payload, 63 - fraction_bits);
      if (qa_bigint_is_zero(&payload)) qa_bigint_set_u64(&payload, 1);
      qa_bigint_or(out, out, &payload);
      qa_bigint_free(&payload);
    }
    qa_bigint_free(&sign_bit); qa_bigint_free(&explicit_bit);
    return;
  }
  qa_bigint_free(&explicit_bit);
  if (qa_bigint_is_zero(&value->coefficient)) { qa_bigint_clone(out, &sign_bit); qa_bigint_free(&sign_bit); return; }
  int64_t top = bi_length(&value->coefficient) - 1 + value->exponent;
  int64_t qa_ = top - fraction_bits, qb = (int64_t)format.minimum_exponent - fraction_bits;
  int64_t quantum = qa_ > qb ? qa_ : qb;
  int64_t shift = value->exponent - quantum;
  qa_bigint significand; qa_bigint_init(&significand);
  if (shift >= 0) qa_bigint_shl(&significand, &value->coefficient, shift);
  else qa_bigint_shr(&significand, &value->coefficient, -shift);
  int64_t exponent = top < format.minimum_exponent ? 0 : top + (1 - format.minimum_exponent);
  qa_bigint fraction; qa_bigint_init(&fraction);
  if (width == 80) qa_bigint_clone(&fraction, &significand);
  else {
    qa_bigint mask; qa_bigint_init(&mask); bi_pow2(&mask, fraction_bits);
    qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
    qa_bigint_sub(&mask, &mask, &one); qa_bigint_free(&one);
    qa_bigint_and(&fraction, &significand, &mask);
    qa_bigint_free(&mask);
  }
  qa_bigint e; qa_bigint_init(&e); qa_bigint_set_i64(&e, exponent);
  qa_bigint es; qa_bigint_init(&es); qa_bigint_shl(&es, &e, storage_bits);
  qa_bigint_or(out, &sign_bit, &es); qa_bigint_or(out, out, &fraction);
  qa_bigint_free(&sign_bit); qa_bigint_free(&significand); qa_bigint_free(&fraction);
  qa_bigint_free(&e); qa_bigint_free(&es);
}
static bool propagate_nan(qa_binary_result *out, const qa_binary_value *left, const qa_binary_value *right, qa_fp_nan_selection selection) {
  if (left->kind == QA_BINARY_UNSUPPORTED || right->kind == QA_BINARY_UNSUPPORTED) {
    out->value = qa_fp_indefinite(); out->flags = QA_FP_INVALID; out->rounded_up = false; return true;
  }
  if (left->kind != QA_BINARY_NAN && right->kind != QA_BINARY_NAN) return false;
  int flags = (left->kind == QA_BINARY_NAN && left->signaling) || (right->kind == QA_BINARY_NAN && right->signaling) ? QA_FP_INVALID : 0;
  const qa_binary_value *selected = left->kind == QA_BINARY_NAN ? left : right;
  if (selection == QA_FP_NAN_X87 && left->kind == QA_BINARY_NAN && right->kind == QA_BINARY_NAN) {
    if (left->signaling != right->signaling) selected = left->signaling ? right : left;
    else {
      int c = qa_bigint_cmp(&right->payload, &left->payload);
      if (c > 0 || (c == 0 && right->sign < left->sign)) selected = right;
    }
  }
  out->value = qa_fp_quiet(selected); out->flags = flags; out->rounded_up = false;
  return true;
}
static void signed_coeff(qa_bigint *out, const qa_binary_value *v, qa_fp_sign sign) {
  qa_bigint_clone(out, &v->coefficient);
  if (sign == 1 && out->sign > 0) out->sign = -1;
}
void qa_fp_arithmetic(qa_binary_result *out, qa_binary_operation op, const qa_binary_value *left,
                      const qa_binary_value *right, qa_binary_format format, qa_fp_rounding mode,
                      qa_fp_nan_selection selection) {
  if (propagate_nan(out, left, right, selection)) return;
  int source_flags = (left->kind == QA_BINARY_FINITE && left->denormal) || (right->kind == QA_BINARY_FINITE && right->denormal) ? QA_FP_DENORMAL_OPERAND : 0;
  qa_fp_sign right_sign = op == QA_FP_SUBTRACT ? (right->sign == 0 ? 1 : 0) : right->sign;
  if (op == QA_FP_ADD || op == QA_FP_SUBTRACT) {
    if (left->kind == QA_BINARY_INFINITY || right->kind == QA_BINARY_INFINITY) {
      if (left->kind == QA_BINARY_INFINITY && right->kind == QA_BINARY_INFINITY && left->sign != right_sign) {
        out->value = qa_fp_indefinite(); out->flags = QA_FP_INVALID | source_flags; out->rounded_up = false; return;
      }
      out->value = value_init(QA_BINARY_INFINITY, left->kind == QA_BINARY_INFINITY ? left->sign : right_sign);
      out->flags = source_flags; out->rounded_up = false; return;
    }
    int64_t exponent = left->exponent < right->exponent ? left->exponent : right->exponent;
    qa_bigint a; qa_bigint_init(&a); signed_coeff(&a, left, left->sign); qa_bigint_shl(&a, &a, left->exponent - exponent);
    qa_bigint b; qa_bigint_init(&b); signed_coeff(&b, right, right_sign); qa_bigint_shl(&b, &b, right->exponent - exponent);
    qa_bigint sum; qa_bigint_init(&sum); qa_bigint_add(&sum, &a, &b);
    qa_bigint_free(&a); qa_bigint_free(&b);
    qa_fp_sign sign = sum.sign < 0 ? 1 : sum.sign > 0 ? 0 : left->sign == right_sign ? left->sign : mode == QA_FP_DOWN ? 1 : 0;
    if (sum.sign < 0) sum.sign = 1;
    qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
    qa_fp_round_rational(out, sign, &sum, &one, exponent, format, mode);
    qa_bigint_free(&sum); qa_bigint_free(&one);
    out->flags |= source_flags;
    return;
  }
  qa_fp_sign sign = left->sign == right->sign ? 0 : 1;
  if (op == QA_FP_MULTIPLY) {
    if (left->kind == QA_BINARY_INFINITY || right->kind == QA_BINARY_INFINITY) {
      bool zero_op = (left->kind == QA_BINARY_FINITE && qa_bigint_is_zero(&left->coefficient)) ||
                     (right->kind == QA_BINARY_FINITE && qa_bigint_is_zero(&right->coefficient));
      if (zero_op) { out->value = qa_fp_indefinite(); out->flags = QA_FP_INVALID | source_flags; out->rounded_up = false; return; }
      out->value = value_init(QA_BINARY_INFINITY, sign); out->flags = source_flags; out->rounded_up = false; return;
    }
    qa_bigint prod; qa_bigint_init(&prod); qa_bigint_mul(&prod, &left->coefficient, &right->coefficient);
    qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
    qa_fp_round_rational(out, sign, &prod, &one, left->exponent + right->exponent, format, mode);
    qa_bigint_free(&prod); qa_bigint_free(&one);
    out->flags |= source_flags;
    return;
  }
  if (left->kind == QA_BINARY_INFINITY && right->kind == QA_BINARY_INFINITY) {
    out->value = qa_fp_indefinite(); out->flags = QA_FP_INVALID | source_flags; out->rounded_up = false; return;
  }
  if (left->kind == QA_BINARY_INFINITY) { out->value = value_init(QA_BINARY_INFINITY, sign); out->flags = source_flags; out->rounded_up = false; return; }
  if (right->kind == QA_BINARY_INFINITY) { out->value = qa_fp_zero(sign); out->flags = source_flags; out->rounded_up = false; return; }
  if (qa_bigint_is_zero(&right->coefficient)) {
    bool lzero = qa_bigint_is_zero(&left->coefficient);
    if (lzero) out->value = qa_fp_indefinite(); else out->value = value_init(QA_BINARY_INFINITY, sign);
    out->flags = (lzero ? QA_FP_INVALID : QA_FP_ZERO_DIVIDE) | source_flags; out->rounded_up = false; return;
  }
  qa_fp_round_rational(out, sign, &left->coefficient, &right->coefficient, left->exponent - right->exponent, format, mode);
  out->flags |= source_flags;
}
qa_fp_comparison qa_fp_compare_binary(const qa_binary_value *left, const qa_binary_value *right) {
  if (left->kind == QA_BINARY_NAN || right->kind == QA_BINARY_NAN || left->kind == QA_BINARY_UNSUPPORTED || right->kind == QA_BINARY_UNSUPPORTED) return QA_FP_UNORDERED;
  if (left->kind == QA_BINARY_FINITE && right->kind == QA_BINARY_FINITE && qa_bigint_is_zero(&left->coefficient) && qa_bigint_is_zero(&right->coefficient)) return QA_FP_EQUAL;
  if (left->sign != right->sign) return left->sign == 1 ? QA_FP_LESS : QA_FP_GREATER;
  int comparison;
  if (left->kind == QA_BINARY_INFINITY) comparison = right->kind == QA_BINARY_INFINITY ? 0 : 1;
  else if (right->kind == QA_BINARY_INFINITY) comparison = -1;
  else comparison = compare_scaled(&left->coefficient, &right->coefficient, left->exponent - right->exponent);
  if (left->sign == 1) comparison = -comparison;
  return comparison < 0 ? QA_FP_LESS : comparison > 0 ? QA_FP_GREATER : QA_FP_EQUAL;
}
void qa_fp_integer_conversion_apply(qa_fp_integer_conversion *out, const qa_binary_value *value, int width, qa_fp_rounding mode) {
  qa_bigint_init(&out->value);
  qa_bigint bad; qa_bigint_init(&bad); bi_pow2(&bad, width - 1); if (bad.sign > 0) bad.sign = -1;
  if (value->kind != QA_BINARY_FINITE) {
    qa_bigint_clone(&out->value, &bad); out->flags = QA_FP_INVALID; out->rounded_up = false;
    qa_bigint_free(&bad); return;
  }
  qa_bigint n; qa_bigint_init(&n); qa_bigint d; qa_bigint_init(&d);
  if (value->exponent >= 0) { qa_bigint_shl(&n, &value->coefficient, value->exponent); qa_bigint_set_u64(&d, 1); }
  else { qa_bigint_clone(&n, &value->coefficient); bi_pow2(&d, -value->exponent); }
  rounded_quotient_t rq; rounded_quotient(&rq, &n, &d, value->sign, mode);
  qa_bigint_free(&n); qa_bigint_free(&d);
  qa_bigint signed_v; qa_bigint_init(&signed_v); qa_bigint_clone(&signed_v, &rq.quotient);
  if (value->sign == 1 && signed_v.sign > 0) signed_v.sign = -1;
  qa_bigint limit; qa_bigint_init(&limit); bi_pow2(&limit, width - 1);
  if (qa_bigint_cmp(&signed_v, &bad) < 0 || qa_bigint_cmp(&signed_v, &limit) >= 0) {
    qa_bigint_clone(&out->value, &bad); out->flags = QA_FP_INVALID; out->rounded_up = false;
  } else {
    qa_bigint_clone(&out->value, &signed_v);
    out->flags = rq.inexact ? QA_FP_PRECISION : 0; out->rounded_up = rq.up;
  }
  qa_bigint_free(&bad); qa_bigint_free(&signed_v); qa_bigint_free(&limit); qa_bigint_free(&rq.quotient);
}
void qa_fp_round_integral(qa_binary_result *out, const qa_binary_value *value, qa_fp_rounding mode) {
  if (value->kind != QA_BINARY_FINITE) { qa_fp_convert_binary(out, value, qa_binary80_format, mode); return; }
  if (value->exponent >= 0) {
    out->value = value_init(QA_BINARY_FINITE, value->sign);
    qa_binary_value_clone(&out->value, value);
    out->flags = 0; out->rounded_up = false; return;
  }
  qa_bigint d; qa_bigint_init(&d); bi_pow2(&d, -value->exponent);
  rounded_quotient_t rq; rounded_quotient(&rq, &value->coefficient, &d, value->sign, mode);
  qa_bigint_free(&d);
  out->value = value_init(QA_BINARY_FINITE, value->sign);
  qa_bigint_clone(&out->value.coefficient, &rq.quotient);
  out->flags = (rq.inexact ? QA_FP_PRECISION : 0) | (value->denormal ? QA_FP_DENORMAL_OPERAND : 0);
  out->rounded_up = rq.up;
  qa_bigint_free(&rq.quotient);
}
static void integer_sqrt(qa_bigint *out, const qa_bigint *value) {
  qa_bigint two; qa_bigint_init(&two); qa_bigint_set_u64(&two, 2);
  if (qa_bigint_cmp(value, &two) < 0) { qa_bigint_clone(out, value); qa_bigint_free(&two); return; }
  qa_bigint_free(&two);
  int64_t shift = (bi_length(value) + 1) / 2; /* ceil(len/2) */
  qa_bigint est; qa_bigint_init(&est); bi_pow2(&est, shift);
  for (;;) {
    qa_bigint quot; qa_bigint_init(&quot); qa_bigint_divmod(&quot, NULL, value, &est);
    qa_bigint sum; qa_bigint_init(&sum); qa_bigint_add(&sum, &est, &quot);
    qa_bigint_free(&quot);
    qa_bigint next; qa_bigint_init(&next); qa_bigint_shr(&next, &sum, 1);
    qa_bigint_free(&sum);
    if (qa_bigint_cmp(&next, &est) >= 0) { qa_bigint_free(&next); break; }
    qa_bigint_free(&est); est = next;
  }
  qa_bigint_clone(out, &est);
  qa_bigint_free(&est);
}
void qa_fp_square_root(qa_binary_result *out, const qa_binary_value *value, qa_binary_format format, qa_fp_rounding mode) {
  if (value->kind == QA_BINARY_NAN || value->kind == QA_BINARY_UNSUPPORTED) { qa_fp_convert_binary(out, value, format, mode); return; }
  if (value->kind == QA_BINARY_FINITE && qa_bigint_is_zero(&value->coefficient)) {
    out->value = value_init(QA_BINARY_FINITE, value->sign); qa_binary_value_clone(&out->value, value);
    out->flags = 0; out->rounded_up = false; return;
  }
  if (value->sign == 1) { out->value = qa_fp_indefinite(); out->flags = QA_FP_INVALID; out->rounded_up = false; return; }
  if (value->kind == QA_BINARY_INFINITY) {
    out->value = value_init(QA_BINARY_INFINITY, value->sign); out->flags = 0; out->rounded_up = false; return;
  }
  int64_t total = bi_length(&value->coefficient) - 1 + value->exponent;
  int64_t top = total >= 0 ? total / 2 : -((-total + 1) / 2); /* Math.floor(/2) */
  int64_t q1 = top - format.precision + 1, q2 = (int64_t)format.minimum_exponent - format.precision + 1;
  int64_t quantum = q1 > q2 ? q1 : q2;
  int64_t shift = value->exponent - quantum * 2;
  qa_bigint num; qa_bigint_init(&num); qa_bigint den; qa_bigint_init(&den);
  if (shift >= 0) { qa_bigint_shl(&num, &value->coefficient, shift); qa_bigint_set_u64(&den, 1); }
  else { qa_bigint_clone(&num, &value->coefficient); bi_pow2(&den, -shift); }
  qa_bigint whole; qa_bigint_init(&whole); qa_bigint_divmod(&whole, NULL, &num, &den);
  qa_bigint fl; qa_bigint_init(&fl); integer_sqrt(&fl, &whole);
  qa_bigint_free(&whole);
  qa_bigint sq; qa_bigint_init(&sq); qa_bigint_mul(&sq, &fl, &fl); qa_bigint_mul(&sq, &sq, &den);
  bool inexact = qa_bigint_cmp(&sq, &num) != 0;
  qa_bigint_free(&sq);
  /* halfway = (2*floor+1)^2 * den */
  qa_bigint two; qa_bigint_init(&two); qa_bigint_set_u64(&two, 2);
  qa_bigint t; qa_bigint_init(&t); qa_bigint_mul(&t, &fl, &two);
  qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
  qa_bigint_add(&t, &t, &one); qa_bigint_mul(&t, &t, &t); qa_bigint_mul(&t, &t, &den);
  qa_bigint num4; qa_bigint_init(&num4); qa_bigint four; qa_bigint_init(&four); qa_bigint_set_u64(&four, 4);
  qa_bigint_mul(&num4, &num, &four);
  bool up = false;
  if (inexact) {
    if (mode == QA_FP_UP) up = true;
    else if (mode == QA_FP_NEAREST) { int c = qa_bigint_cmp(&num4, &t); up = c > 0 || (c == 0 && qa_bigint_test_bit(&fl, 0)); }
  }
  out->value = value_init(QA_BINARY_FINITE, 0);
  qa_bigint_clone(&out->value.coefficient, &fl);
  if (up) qa_bigint_add(&out->value.coefficient, &out->value.coefficient, &one);
  out->value.exponent = quantum;
  out->flags = (inexact ? QA_FP_PRECISION : 0) | (value->denormal ? QA_FP_DENORMAL_OPERAND : 0);
  out->rounded_up = up;
  qa_bigint_free(&num); qa_bigint_free(&den); qa_bigint_free(&fl); qa_bigint_free(&two);
  qa_bigint_free(&one); qa_bigint_free(&t); qa_bigint_free(&num4); qa_bigint_free(&four);
}
/* contracts.ts throw channel */
void qa_numeric_context_init(qa_numeric_context *ctx, qa_guest_processor_state *state,
                             qa_mapped_guest_memory *memory, const qa_numeric_instruction *instruction) {
  ctx->state = state; ctx->memory = memory; ctx->instruction = *instruction;
  ctx->throw_buf = NULL; ctx->throw_kind = QA_THROW_NONE; ctx->throw_vector = 0; ctx->throw_detail[0] = 0;
}
static void throw_to(qa_numeric_context *ctx, int kind, int vector, const char *detail) {
  ctx->throw_kind = kind; ctx->throw_vector = vector;
  snprintf(ctx->throw_detail, sizeof ctx->throw_detail, "%s", detail != NULL ? detail : "");
  if (ctx->throw_buf != NULL) longjmp(*ctx->throw_buf, 1);
  qa_range_error("%s", detail != NULL ? detail : "numeric fault");
}
void qa_numeric_fault(qa_numeric_context *ctx, int vector, const char *detail) { throw_to(ctx, QA_THROW_FAULT, vector, detail); }
void qa_numeric_throw_unsupported(qa_numeric_context *ctx, const char *detail) { throw_to(ctx, QA_THROW_UNSUPPORTED, 0, detail); }
void qa_numeric_throw_deferred(qa_numeric_context *ctx) { throw_to(ctx, QA_THROW_DEFERRED, 0, ""); }
void qa_numeric_set_result(qa_numeric_result *out, qa_numeric_result_kind kind, int vector, const char *detail) {
  out->kind = kind; out->vector = vector;
  snprintf(out->detail, sizeof out->detail, "%s", detail != NULL ? detail : "");
}
