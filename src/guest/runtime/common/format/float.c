/* Port of src/guest/runtime/common/format/float.ts. GPL-2.0-or-later. */
#include "qa/guest/runtime/common/format/float.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *qa_format_rounding_name(qa_format_rounding mode) {
  switch (mode) {
    case QA_FORMAT_NEAREST: return "nearest";
    case QA_FORMAT_DOWN: return "down";
    case QA_FORMAT_UP: return "up";
    case QA_FORMAT_ZERO: return "zero";
    default: return "legacy-nearest";
  }
}
static char *dup_str(const char *s) {
  size_t n = strlen(s);
  char *r = (char *)malloc(n + 1);
  if (r != NULL) memcpy(r, s, n + 1);
  return r;
}
/* quotient(): magnitude division with TS rounding (sign is 0|1). */
static void fmt_quotient(qa_bigint *out, const qa_bigint *num, const qa_bigint *den, int sign, qa_format_rounding mode) {
  qa_bigint rem; qa_bigint_init(&rem);
  qa_bigint_divmod(out, &rem, num, den);
  bool up = false;
  if (!qa_bigint_is_zero(&rem)) {
    if (mode == QA_FORMAT_NEAREST || mode == QA_FORMAT_LEGACY_NEAREST) {
      qa_bigint two; qa_bigint_init(&two); qa_bigint_set_u64(&two, 2);
      qa_bigint dbl; qa_bigint_init(&dbl); qa_bigint_mul(&dbl, &rem, &two);
      int c = qa_bigint_cmp(&dbl, den);
      up = mode == QA_FORMAT_NEAREST ? (c > 0 || (c == 0 && qa_bigint_test_bit(out, 0))) : c >= 0;
      qa_bigint_free(&two); qa_bigint_free(&dbl);
    } else if (mode == QA_FORMAT_UP) up = sign == 0;
    else if (mode == QA_FORMAT_DOWN) up = sign == 1;
  }
  if (up) { qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1); qa_bigint_add(out, out, &one); qa_bigint_free(&one); }
  qa_bigint_free(&rem);
}
static void fmt_ratio(const qa_binary_value *value, qa_bigint *num, qa_bigint *den) {
  if (value->exponent >= 0) {
    qa_bigint_shl(num, &value->coefficient, value->exponent);
    qa_bigint_set_u64(den, 1);
  } else {
    qa_bigint_clone(num, &value->coefficient);
    qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
    qa_bigint_shl(den, &one, -value->exponent);
    qa_bigint_free(&one);
  }
}
static void fmt_scaled(qa_bigint *out, const qa_binary_value *value, int decimal_places, qa_format_rounding mode) {
  qa_bigint num; qa_bigint_init(&num); qa_bigint den; qa_bigint_init(&den);
  fmt_ratio(value, &num, &den);
  if (decimal_places >= 0) {
    qa_bigint p; qa_bigint_init(&p); qa_bigint_pow_u32(&p, 10, (uint32_t)decimal_places);
    qa_bigint_mul(&num, &num, &p);
    qa_bigint_free(&p);
    fmt_quotient(out, &num, &den, value->sign, mode);
  } else {
    qa_bigint p; qa_bigint_init(&p); qa_bigint_pow_u32(&p, 10, (uint32_t)-decimal_places);
    qa_bigint_mul(&den, &den, &p);
    qa_bigint_free(&p);
    fmt_quotient(out, &num, &den, value->sign, mode);
  }
  qa_bigint_free(&num); qa_bigint_free(&den);
}
static int decimal_digit_count(const qa_bigint *v) {
  char *s = qa_bigint_to_string(v, 10);
  if (s == NULL) return 1;
  int n = (int)strlen(s);
  if (n > 0 && s[0] == '-') n--;
  free(s);
  return n;
}
static int fmt_decimal_exponent(const qa_binary_value *value) {
  if (qa_bigint_is_zero(&value->coefficient)) return 0;
  qa_bigint n; qa_bigint_init(&n); qa_bigint d; qa_bigint_init(&d);
  fmt_ratio(value, &n, &d);
  int exponent = decimal_digit_count(&n) - decimal_digit_count(&d);
  bool decrease;
  if (exponent >= 0) {
    qa_bigint p; qa_bigint_init(&p); qa_bigint_pow_u32(&p, 10, (uint32_t)exponent);
    qa_bigint t; qa_bigint_init(&t); qa_bigint_mul(&t, &d, &p);
    decrease = qa_bigint_cmp(&n, &t) < 0;
    qa_bigint_free(&p); qa_bigint_free(&t);
  } else {
    qa_bigint p; qa_bigint_init(&p); qa_bigint_pow_u32(&p, 10, (uint32_t)-exponent);
    qa_bigint t; qa_bigint_init(&t); qa_bigint_mul(&t, &n, &p);
    decrease = qa_bigint_cmp(&t, &d) < 0;
    qa_bigint_free(&p); qa_bigint_free(&t);
  }
  if (decrease) exponent--;
  qa_bigint_free(&n); qa_bigint_free(&d);
  return exponent;
}
/* padStart(len, '0') */
static char *pad_start(const char *digits, size_t len) {
  size_t cur = strlen(digits);
  if (cur >= len) return dup_str(digits);
  char *r = (char *)malloc(len + 1);
  if (r == NULL) return NULL;
  memset(r, '0', len - cur);
  memcpy(r + (len - cur), digits, cur + 1);
  return r;
}
static char *fixed_digits(const char *digits, int places, bool point) {
  if (places == 0) {
    size_t n = strlen(digits);
    char *r = (char *)malloc(n + 2);
    if (r == NULL) return NULL;
    memcpy(r, digits, n);
    if (point) r[n++] = '.';
    r[n] = 0;
    return r;
  }
  char *padded = pad_start(digits, (size_t)places + 1);
  if (padded == NULL) return NULL;
  size_t n = strlen(padded);
  char *r = (char *)malloc(n + 2);
  if (r == NULL) { free(padded); return NULL; }
  memcpy(r, padded, n - (size_t)places);
  r[n - (size_t)places] = '.';
  memcpy(r + (n - (size_t)places) + 1, padded + (n - (size_t)places), (size_t)places + 1);
  free(padded);
  return r;
}
static char *exponent_suffix(int exponent, char marker, int minimum) {
  char num[32];
  snprintf(num, sizeof num, "%d", exponent < 0 ? -exponent : exponent);
  char *padded = pad_start(num, minimum > 0 ? (size_t)minimum : 0);
  if (padded == NULL) return NULL;
  size_t n = strlen(padded);
  char *r = (char *)malloc(n + 3);
  if (r == NULL) { free(padded); return NULL; }
  r[0] = marker;
  r[1] = exponent < 0 ? '-' : '+';
  memcpy(r + 2, padded, n + 1);
  free(padded);
  return r;
}
static void strip_trailing_zeros(char *s) {
  size_t n = strlen(s);
  while (n > 0 && s[n - 1] == '0') s[--n] = 0;
}
typedef struct { char *digits; int exponent; } significant_t;
static bool fmt_significant(significant_t *out, const qa_binary_value *value, int precision, qa_format_rounding mode) {
  int exponent = fmt_decimal_exponent(value);
  qa_bigint scaled; qa_bigint_init(&scaled);
  fmt_scaled(&scaled, value, precision - 1 - exponent, mode);
  char *digits = qa_bigint_to_string(&scaled, 10);
  qa_bigint_free(&scaled);
  if (digits == NULL) return false;
  if ((int)strlen(digits) > precision) { exponent++; digits[precision] = 0; }
  char *padded = pad_start(digits, precision > 0 ? (size_t)precision : 0);
  free(digits);
  if (padded == NULL) return false;
  out->digits = padded;
  out->exponent = exponent;
  return true;
}
static char *fmt_hex(const qa_binary_value *value, bool precision_present, int precision, bool alternate,
                     qa_format_rounding mode, bool windows, bool extended) {
  int fraction_bits = extended ? 63 : 52;
  int exponent = qa_bigint_is_zero(&value->coefficient) ? 0
    : value->denormal ? (extended ? -16382 : -1022) : (int)value->exponent + fraction_bits;
  /* glibc writes x87's explicit integer bit as the top bit of one hex digit. */
  int leading_bits = extended && !windows ? 4 : 1;
  exponent -= qa_bigint_is_zero(&value->coefficient) ? 0 : leading_bits - 1;
  int fraction = fraction_bits - leading_bits + 1;
  int natural = (fraction + 3) / 4; /* Math.ceil */
  int places = precision_present ? precision : windows ? 13 : natural;
  int shift = places * 4 - fraction;
  qa_bigint digits_value; qa_bigint_init(&digits_value);
  if (shift >= 0) qa_bigint_shl(&digits_value, &value->coefficient, shift);
  else {
    qa_bigint den; qa_bigint_init(&den);
    qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
    qa_bigint_shl(&den, &one, -shift);
    qa_bigint_free(&one);
    fmt_quotient(&digits_value, &value->coefficient, &den, value->sign, mode);
    qa_bigint_free(&den);
  }
  char *digits = qa_bigint_to_string(&digits_value, 16);
  qa_bigint_free(&digits_value);
  if (digits == NULL) return NULL;
  char *padded = pad_start(digits, (size_t)places + 1);
  free(digits);
  if (padded == NULL) return NULL;
  digits = padded;
  if ((int)strlen(digits) > places + 1) {
    char *rebuilt = (char *)malloc((size_t)places + 2);
    if (rebuilt == NULL) { free(digits); return NULL; }
    rebuilt[0] = '1';
    memset(rebuilt + 1, '0', (size_t)places);
    rebuilt[places + 1] = 0;
    free(digits);
    digits = rebuilt;
    exponent += 4;
  }
  size_t dn = strlen(digits);
  size_t head_len = places == 0 ? dn : dn - (size_t)places;
  char *tail = dup_str(places == 0 ? "" : digits + (dn - (size_t)places));
  if (tail == NULL) { free(digits); return NULL; }
  if (!precision_present && !windows) strip_trailing_zeros(tail);
  char *suffix = exponent_suffix(exponent, 'p', 1);
  if (suffix == NULL) { free(digits); free(tail); return NULL; }
  size_t tail_len = strlen(tail);
  bool dot = tail_len > 0 || alternate;
  size_t total = 2 + head_len + (dot ? 1 + tail_len : 0) + strlen(suffix);
  char *r = (char *)malloc(total + 1);
  if (r == NULL) { free(digits); free(tail); free(suffix); return NULL; }
  char *w = r;
  memcpy(w, "0x", 2); w += 2;
  memcpy(w, digits, head_len); w += head_len;
  if (dot) { *w++ = '.'; memcpy(w, tail, tail_len); w += tail_len; }
  memcpy(w, suffix, strlen(suffix) + 1);
  free(digits); free(tail); free(suffix);
  return r;
}
char *qa_format_float(const qa_binary_value *value, char conversion, bool precision_present, int precision,
                      bool alternate, qa_format_rounding mode, bool windows, bool extended, int exponent_digits) {
  char lower = (char)tolower((unsigned char)conversion);
  bool upper = conversion != lower;
  char *text = NULL;
  if (value->kind == QA_BINARY_INFINITY) text = dup_str("inf");
  else if (value->kind == QA_BINARY_UNSUPPORTED) text = dup_str(windows ? "nan(ind)" : "nan");
  else if (value->kind == QA_BINARY_NAN) {
    bool ind = false;
    if (windows && value->sign == 1) {
      qa_bigint expect; qa_bigint_init(&expect);
      qa_bigint one; qa_bigint_init(&one); qa_bigint_set_u64(&one, 1);
      qa_bigint_shl(&expect, &one, 62);
      qa_bigint_free(&one);
      ind = qa_bigint_cmp(&value->payload, &expect) == 0;
      qa_bigint_free(&expect);
    }
    text = dup_str(windows && value->signaling ? "nan(snan)" : ind ? "nan(ind)" : "nan");
  } else if (lower == 'a') {
    text = fmt_hex(value, precision_present, precision, alternate, mode, windows, extended);
  } else if (lower == 'f') {
    int places = precision_present ? precision : 6;
    qa_bigint scaled; qa_bigint_init(&scaled);
    fmt_scaled(&scaled, value, places, mode);
    char *digits = qa_bigint_to_string(&scaled, 10);
    qa_bigint_free(&scaled);
    if (digits != NULL) { text = fixed_digits(digits, places, alternate); free(digits); }
  } else {
    int places = precision_present ? precision : 6;
    int count = lower == 'e' ? places + 1 : places > 1 ? places : 1;
    significant_t sig;
    if (fmt_significant(&sig, value, count, mode)) {
      if (lower == 'e' || sig.exponent < -4 || sig.exponent >= count) {
        char *tail = dup_str(sig.digits + 1);
        if (tail != NULL) {
          if (lower == 'g' && !alternate) strip_trailing_zeros(tail);
          char *suffix = exponent_suffix(sig.exponent, 'e', exponent_digits);
          if (suffix != NULL) {
            size_t tail_len = strlen(tail);
            bool dot = tail_len > 0 || alternate;
            text = (char *)malloc(1 + (dot ? 1 + tail_len : 0) + strlen(suffix) + 1);
            if (text != NULL) {
              char *w = text;
              *w++ = sig.digits[0];
              if (dot) { *w++ = '.'; memcpy(w, tail, tail_len); w += tail_len; }
              memcpy(w, suffix, strlen(suffix) + 1);
            }
            free(suffix);
          }
          free(tail);
        }
      } else {
        int decimal_places = count - 1 - sig.exponent;
        text = fixed_digits(sig.digits, decimal_places, alternate);
        if (text != NULL && !alternate && strchr(text, '.') != NULL) {
          strip_trailing_zeros(text);
          size_t n = strlen(text);
          if (n > 0 && text[n - 1] == '.') text[n - 1] = 0;
        }
      }
      free(sig.digits);
    }
  }
  if (text != NULL && upper) for (char *p = text; *p; p++) *p = (char)toupper((unsigned char)*p);
  return text;
}
