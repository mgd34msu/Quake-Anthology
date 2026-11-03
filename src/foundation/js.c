/* JavaScript number semantics; see include/qa/foundation/js.h. GPL-2.0-or-later. */
#include "qa/foundation/js.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

double qa_js_pow(double base, double exponent) {
  /* JavaScriptCore operationMathPow, checked against bun over random inputs. */
  if (isnan(exponent)) return NAN;
  if (fabs(base) == 1.0 && isinf(exponent)) return NAN;
  if (exponent >= 0.0 && exponent <= 1000.0 && exponent == trunc(exponent)) {
    int32_t remaining = (int32_t)exponent;
    double result = 1.0;
    while (remaining != 0) {
      if (remaining & 1) result *= base;
      base *= base;
      remaining >>= 1;
    }
    return result;
  }
  if (isfinite(base) && base != 0.0) {
    if (exponent == 0.5) return sqrt(base);
    if (exponent == -0.5) return 1.0 / sqrt(base);
  }
  return pow(base, exponent);
}

double qa_js_hypot2(double a, double b) {
  if (isinf(a) || isinf(b)) return INFINITY;
  if (isnan(a) || isnan(b)) return NAN;
  return hypot(a, b);
}

double qa_js_hypot3(double a, double b, double c) {
  /* JavaScriptCore calls the libstdc++ three-argument std::hypot. */
  if (isinf(a) || isinf(b) || isinf(c)) return INFINITY;
  if (isnan(a) || isnan(b) || isnan(c)) return NAN;
  a = fabs(a);
  b = fabs(b);
  c = fabs(c);
  double largest = a > b ? (a > c ? a : c) : (b > c ? b : c);
  if (largest == 0.0) return 0.0;
  a /= largest;
  b /= largest;
  c /= largest;
  return largest * sqrt(a * a + b * b + c * c);
}

double qa_js_hypot(size_t count, const double *values) {
  if (count == 0) return 0.0;
  if (count == 1) return isinf(values[0]) ? INFINITY : fabs(values[0]);
  if (count == 2) return qa_js_hypot2(values[0], values[1]);
  if (count == 3) return qa_js_hypot3(values[0], values[1], values[2]);
  bool infinite = false, not_number = false;
  double largest = 0.0, sum = 0.0;
  for (size_t i = 0; i < count; i++) {
    double value = values[i];
    if (isinf(value)) { infinite = true; continue; }
    if (isnan(value)) { not_number = true; continue; }
    double magnitude = fabs(value);
    if (largest < magnitude) {
      double scaled = largest / magnitude;
      sum = fma(sum, scaled * scaled, 1.0);
      largest = magnitude;
    } else if (largest != 0.0) {
      double scaled = magnitude / largest;
      sum = fma(scaled, scaled, sum);
    }
  }
  if (infinite) return INFINITY;
  if (not_number) return NAN;
  if (largest == 0.0) return 0.0;
  return sqrt(sum) * largest;
}

size_t qa_js_white_space_length(const char *text) {
  const unsigned char *at = (const unsigned char *)text;
  switch (at[0]) {
    case '\t': case '\n': case '\v': case '\f': case '\r': case ' ': return 1;
    case 0xc2: return at[1] == 0xa0 ? 2 : 0;                      /* U+00A0 */
    case 0xe1: return at[1] == 0x9a && at[2] == 0x80 ? 3 : 0;     /* U+1680 */
    case 0xe2:
      if (at[1] == 0x80 && ((at[2] >= 0x80 && at[2] <= 0x8a) ||  /* U+2000-200A */
                            at[2] == 0xa8 || at[2] == 0xa9 ||    /* U+2028-2029 */
                            at[2] == 0xaf)) return 3;            /* U+202F */
      return at[1] == 0x81 && at[2] == 0x9f ? 3 : 0;              /* U+205F */
    case 0xe3: return at[1] == 0x80 && at[2] == 0x80 ? 3 : 0;     /* U+3000 */
    case 0xef: return at[1] == 0xbb && at[2] == 0xbf ? 3 : 0;     /* U+FEFF */
    default: return 0;
  }
}

static const char *skip_white(const char *text) {
  size_t length;
  while ((length = qa_js_white_space_length(text)) != 0) text += length;
  return text;
}

static int digit_value(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return 99;
}

/* strtod over an exact copy of a validated decimal literal (correctly rounded,
 * as JavaScriptCore's parser). */
static double decimal_of(const char *start, size_t length) {
  char small[128];
  char *copy = length < sizeof small ? small : malloc(length + 1);
  if (copy == NULL) qa_range_error("Out of memory converting a numeric string");
  memcpy(copy, start, length);
  copy[length] = '\0';
  double value = strtod(copy, NULL);
  if (copy != small) free(copy);
  return value;
}

/* JavaScriptCore parseInt digit accumulation, with its overflow paths. */
static double radix_digits(const char *digits, size_t length, int radix) {
  double number = 0.0;
  for (size_t i = 0; i < length; i++) number = number * radix + digit_value((unsigned char)digits[i]);
  if (number >= 9007199254740992.0) {
    if (radix == 10) return decimal_of(digits, length);
    if (radix == 2 || radix == 4 || radix == 8 || radix == 16 || radix == 32) {
      number = 0.0;
      double multiplier = 1.0;
      for (size_t i = length; i-- > 0;) {
        if (multiplier == INFINITY) {
          if (digits[i] != '0') { number = INFINITY; break; }
        } else {
          number += digit_value((unsigned char)digits[i]) * multiplier;
        }
        multiplier *= radix;
      }
    }
  }
  return number;
}

double qa_js_parse_int(const char *text, int radix) {
  const char *at = skip_white(text);
  double sign = 1.0;
  if (*at == '-' || *at == '+') {
    if (*at == '-') sign = -1.0;
    at++;
  }
  bool strip_prefix = true;
  if (radix != 0) {
    if (radix < 2 || radix > 36) return NAN;
    if (radix != 16) strip_prefix = false;
  } else {
    radix = 10;
  }
  if (strip_prefix && at[0] == '0' && (at[1] == 'x' || at[1] == 'X')) {
    at += 2;
    radix = 16;
  }
  size_t length = 0;
  while (digit_value((unsigned char)at[length]) < radix) length++;
  if (length == 0) return NAN;
  return sign * radix_digits(at, length, radix);
}

/* Length of the longest StrDecimalLiteral prefix (sign, Infinity, digits, point,
 * exponent), or 0 when none. */
static size_t decimal_literal_length(const char *at, bool *infinity) {
  size_t i = 0;
  *infinity = false;
  if (at[i] == '+' || at[i] == '-') i++;
  if (strncmp(at + i, "Infinity", 8) == 0) {
    *infinity = true;
    return i + 8;
  }
  size_t whole = 0, fraction = 0;
  while (at[i] >= '0' && at[i] <= '9') { i++; whole++; }
  if (at[i] == '.') {
    size_t point = i++;
    while (at[i] >= '0' && at[i] <= '9') { i++; fraction++; }
    if (whole == 0 && fraction == 0) return 0;
    if (fraction == 0) i = point + 1;
  }
  if (whole == 0 && fraction == 0) return 0;
  if (at[i] == 'e' || at[i] == 'E') {
    size_t mark = i++;
    if (at[i] == '+' || at[i] == '-') i++;
    size_t exponent = 0;
    while (at[i] >= '0' && at[i] <= '9') { i++; exponent++; }
    if (exponent == 0) i = mark;
  }
  return i;
}

double qa_js_parse_float(const char *text) {
  const char *at = skip_white(text);
  bool infinity;
  size_t length = decimal_literal_length(at, &infinity);
  if (length == 0) return NAN;
  if (infinity) return at[0] == '-' ? -INFINITY : INFINITY;
  return decimal_of(at, length);
}

double qa_js_string_to_number(const char *text) {
  const char *start = skip_white(text);
  size_t end = strlen(start);
  /* Trim trailing white space: rescan forward, remembering the last non-space end. */
  size_t content = 0;
  for (size_t i = 0; i < end;) {
    size_t length = qa_js_white_space_length(start + i);
    if (length == 0) {
      i++;
      content = i;
    } else {
      i += length;
    }
  }
  if (content == 0) return 0.0;
  if (start[0] == '0' && content > 2) {
    int radix = start[1] == 'x' || start[1] == 'X' ? 16 : start[1] == 'o' || start[1] == 'O' ? 8
              : start[1] == 'b' || start[1] == 'B' ? 2 : 0;
    if (radix != 0) {
      for (size_t i = 2; i < content; i++) {
        if (digit_value((unsigned char)start[i]) >= radix) return NAN;
      }
      return radix_digits(start + 2, content - 2, radix);
    }
  }
  bool infinity;
  size_t length = decimal_literal_length(start, &infinity);
  if (length != content) return NAN;
  if (infinity) return start[0] == '-' ? -INFINITY : INFINITY;
  return decimal_of(start, length);
}

size_t qa_js_to_fixed(double value, int digits, char *buffer, size_t capacity) {
  if (digits < 0 || digits > 100) qa_range_error("toFixed() argument must be between 0 and 100");
  char text[1200];
  if (isnan(value) || isinf(value) || fabs(value) >= 1e21) {
    /* String(value) for these: NaN, Infinity, -Infinity, or an exponent form. */
    if (isnan(value)) snprintf(text, sizeof text, "NaN");
    else if (isinf(value)) snprintf(text, sizeof text, value < 0 ? "-Infinity" : "Infinity");
    else {
      /* Shortest round-trip digits in exponent form, as Number::toString. */
      int precision = 1;
      for (; precision <= 17; precision++) {
        snprintf(text, sizeof text, "%.*e", precision - 1, value);
        if (strtod(text, NULL) == value) break;
      }
      char mantissa[40];
      int exponent = 0;
      char *e = strchr(text, 'e');
      exponent = atoi(e + 1);
      *e = '\0';
      size_t m = 0;
      for (const char *p = text; *p != '\0'; p++) {
        if (*p != '.') mantissa[m++] = *p;
      }
      mantissa[m] = '\0';
      /* Trim trailing zeros of the significand. */
      while (m > 1 && mantissa[m - 1] == '0' && mantissa[m - 2] != '-') mantissa[--m] = '\0';
      const char *sign = mantissa[0] == '-' ? "-" : "";
      const char *significand = mantissa[0] == '-' ? mantissa + 1 : mantissa;
      if (strlen(significand) == 1) snprintf(text, sizeof text, "%s%se+%d", sign, significand, exponent);
      else snprintf(text, sizeof text, "%s%c.%se+%d", sign, significand[0], significand + 1, exponent);
    }
  } else {
    bool negative = value < 0;
    /* The exact decimal expansion: binary64 fractions end within 1074 digits. */
    snprintf(text + 2, sizeof text - 2, "%.1074f", fabs(value));
    text[0] = '-'; /* sign slot */
    text[1] = '0'; /* carry room */
    char *point = strchr(text + 2, '.');
    size_t keep = (size_t)(point - text) + (size_t)digits - (digits == 0); /* last kept digit */
    /* toFixed picks the larger n of two equally near: an exact tie rounds up. */
    bool up = point[digits + 1] >= '5';
    *(digits == 0 ? point : point + digits + 1) = '\0';
    if (up) {
      for (size_t i = keep; ; i--) {
        if (text[i] == '.') continue;
        if (text[i] == '9') { text[i] = '0'; continue; }
        text[i]++;
        break;
      }
    }
    char *start = text[1] == '0' ? text + 2 : text + 1;
    if (negative) *--start = '-';
    memmove(text, start, strlen(start) + 1);
  }
  size_t length = strlen(text);
  if (capacity > 0) {
    size_t copy = length < capacity - 1 ? length : capacity - 1;
    memcpy(buffer, text, copy);
    buffer[copy] = '\0';
  }
  return length;
}

static void merge_sort(char *base, char *scratch, size_t count, size_t size, qa_js_compare_fn compare,
                       void *ctx) {
  if (count < 2) return;
  if (count <= 8) {
    for (size_t i = 1; i < count; i++) {
      memcpy(scratch, base + i * size, size);
      size_t j = i;
      while (j > 0 && compare(base + (j - 1) * size, scratch, ctx) > 0) {
        memcpy(base + j * size, base + (j - 1) * size, size);
        j--;
      }
      memcpy(base + j * size, scratch, size);
    }
    return;
  }
  size_t half = count / 2;
  merge_sort(base, scratch, half, size, compare, ctx);
  merge_sort(base + half * size, scratch, count - half, size, compare, ctx);
  if (compare(base + (half - 1) * size, base + half * size, ctx) <= 0) return;
  memcpy(scratch, base, half * size);
  size_t left = 0, right = half, out = 0;
  while (left < half && right < count) {
    /* Take from the right only when strictly smaller: equal elements keep order. */
    if (compare(base + right * size, scratch + left * size, ctx) < 0) {
      memmove(base + out * size, base + right * size, size);
      right++;
    } else {
      memcpy(base + out * size, scratch + left * size, size);
      left++;
    }
    out++;
  }
  if (left < half) memcpy(base + out * size, scratch + left * size, (half - left) * size);
}

void qa_js_stable_sort(void *base, size_t count, size_t size, qa_js_compare_fn compare, void *ctx) {
  if (count < 2 || size == 0) return;
  char small[512];
  size_t need = (count / 2 + 8) * size;
  if (need < size) need = size;
  char *scratch = need <= sizeof small ? small : malloc(need);
  if (scratch == NULL) {
    /* Out of memory: stable insertion sort with one element of scratch. */
    char *one = malloc(size);
    if (one == NULL) qa_range_error("Out of memory sorting %zu elements", count);
    for (size_t i = 1; i < count; i++) {
      char *items = base;
      memcpy(one, items + i * size, size);
      size_t j = i;
      while (j > 0 && compare(items + (j - 1) * size, one, ctx) > 0) {
        memcpy(items + j * size, items + (j - 1) * size, size);
        j--;
      }
      memcpy(items + j * size, one, size);
    }
    free(one);
    return;
  }
  merge_sort(base, scratch, count, size, compare, ctx);
  if (scratch != small) free(scratch);
}

static int plain_compare(const void *a, const void *b, void *ctx) {
  int (*compare)(const void *, const void *);
  memcpy(&compare, ctx, sizeof compare);
  return compare(a, b);
}

void qa_js_sort(void *base, size_t count, size_t size, int (*compare)(const void *, const void *)) {
  qa_js_stable_sort(base, count, size, plain_compare, &compare);
}
