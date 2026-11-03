/* Number.prototype.toExponential/toPrecision; see include/qa/foundation/js_parse_extra.h.
 * GPL-2.0-or-later. */
#include "qa/foundation/js_parse_extra.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* `count` significant digits of |value| (finite, nonzero) rounded as JS does: of the
 * two nearest, the larger. Returns the decimal exponent of the first digit. */
static int significant_digits(double value, int count, char *out) {
  /* 800 significant digits hold every binary64 value exactly (at most 767 are needed). */
  char exact[900];
  snprintf(exact, sizeof exact, "%.799e", fabs(value));
  char digits[801];
  size_t n = 0;
  const char *at = exact;
  for (; *at != 'e'; at++) {
    if (*at != '.') digits[n++] = *at;
  }
  digits[n] = '\0';
  int exponent = atoi(at + 1);
  bool up = digits[count] >= '5'; /* the expansion is exact: '5' then anything is >= half */
  memcpy(out, digits, (size_t)count);
  out[count] = '\0';
  if (up) {
    int i = count - 1;
    while (i >= 0 && out[i] == '9') out[i--] = '0';
    if (i >= 0) {
      out[i]++;
    } else {
      out[0] = '1'; /* 9.99 -> 10.0: one more digit of magnitude */
      exponent++;
    }
  }
  return exponent;
}

static size_t finish(const char *text, char *buffer, size_t capacity) {
  size_t length = strlen(text);
  if (capacity > 0) {
    size_t copy = length < capacity - 1 ? length : capacity - 1;
    memcpy(buffer, text, copy);
    buffer[copy] = '\0';
  }
  return length;
}

static size_t non_finite(double value, char *buffer, size_t capacity) {
  return finish(isnan(value) ? "NaN" : value < 0 ? "-Infinity" : "Infinity", buffer, capacity);
}

size_t qa_js_to_exponential(double value, int digits, char *buffer, size_t capacity) {
  if (!isfinite(value)) return non_finite(value, buffer, capacity);
  if (digits < 0 || digits > 100) qa_range_error("toExponential() argument must be between 0 and 100");
  char m[128], text[QA_JS_TO_PRECISION_CAPACITY + 16];
  int exponent = 0;
  if (value == 0) {
    memset(m, '0', (size_t)digits + 1);
    m[digits + 1] = '\0';
  } else {
    exponent = significant_digits(value, digits + 1, m);
  }
  size_t at = 0;
  if (value < 0) text[at++] = '-';
  text[at++] = m[0];
  if (digits > 0) {
    text[at++] = '.';
    memcpy(text + at, m + 1, (size_t)digits);
    at += (size_t)digits;
  }
  snprintf(text + at, sizeof text - at, "e%c%d", exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
  return finish(text, buffer, capacity);
}

size_t qa_js_to_precision(double value, int precision, char *buffer, size_t capacity) {
  if (!isfinite(value)) return non_finite(value, buffer, capacity);
  if (precision < 1 || precision > 100) qa_range_error("toPrecision() argument must be between 1 and 100");
  char m[128], text[QA_JS_TO_PRECISION_CAPACITY + 16];
  int exponent = 0;
  if (value == 0) {
    memset(m, '0', (size_t)precision);
    m[precision] = '\0';
  } else {
    exponent = significant_digits(value, precision, m);
  }
  size_t at = 0;
  if (value < 0) text[at++] = '-';
  if (exponent < -6 || exponent >= precision) {
    text[at++] = m[0];
    if (precision > 1) {
      text[at++] = '.';
      memcpy(text + at, m + 1, (size_t)precision - 1);
      at += (size_t)precision - 1;
    }
    snprintf(text + at, sizeof text - at, "e%c%d", exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
  } else if (exponent >= 0) {
    memcpy(text + at, m, (size_t)exponent + 1);
    at += (size_t)exponent + 1;
    if (exponent + 1 < precision) {
      text[at++] = '.';
      memcpy(text + at, m + exponent + 1, (size_t)(precision - exponent - 1));
      at += (size_t)(precision - exponent - 1);
    }
    text[at] = '\0';
  } else {
    text[at++] = '0';
    text[at++] = '.';
    for (int i = 0; i < -(exponent + 1); i++) text[at++] = '0';
    memcpy(text + at, m, (size_t)precision);
    at += (size_t)precision;
    text[at] = '\0';
  }
  return finish(text, buffer, capacity);
}
