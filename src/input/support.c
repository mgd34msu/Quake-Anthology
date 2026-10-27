/* See include/qa/input/support.h. GPL-2.0-or-later. */
#include "qa/input/support.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

noreturn void qa_input_error(const char *format, ...) {
  va_list args;
  va_start(args, format);
  fputs("Error: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  abort();
}

char *qa_input_dup(const char *text) {
  if (text == NULL) return NULL;
  size_t length = strlen(text);
  char *out = (char *)malloc(length + 1);
  if (out != NULL) memcpy(out, text, length + 1);
  return out;
}

char *qa_input_dup_range(const char *text, size_t length) {
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  memcpy(out, text, length);
  out[length] = '\0';
  return out;
}

int32_t qa_input_to_int32(double value) {
  if (!isfinite(value) || value == 0) return 0;
  double magnitude = fmod(fabs(value), 4294967296.0);
  int64_t whole = (int64_t)magnitude;
  if (value < 0) whole = -whole;
  if (whole >= 2147483648LL) whole -= 4294967296LL;
  if (whole < -2147483648LL) whole += 4294967296LL;
  return (int32_t)whole;
}

char *qa_input_js_number(double value) {
  if (isnan(value)) return qa_input_dup("NaN");
  if (isinf(value)) return qa_input_dup(value < 0 ? "-Infinity" : "Infinity");
  if (value == 0) return qa_input_dup("0");
  char buffer[64];
  if (value == trunc(value) && fabs(value) < 1e21) {
    snprintf(buffer, sizeof buffer, "%.0f", value);
    return qa_input_dup(buffer);
  }
  /* Shortest %g rendering that parses back to the same double. */
  for (int precision = 1; precision <= 17; precision++) {
    snprintf(buffer, sizeof buffer, "%.*g", precision, value);
    if (strtod(buffer, NULL) == value) break;
  }
  /* JS exponent form drops the zero padding %g emits ("1e-07"). */
  char *exponent = strchr(buffer, 'e');
  if (exponent != NULL) {
    char *digits = exponent + 1;
    if (*digits == '+' || *digits == '-') digits++;
    while (digits[0] == '0' && digits[1] != '\0') {
      memmove(digits, digits + 1, strlen(digits + 1) + 1);
    }
  }
  return qa_input_dup(buffer);
}

bool qa_input_js_space(unsigned char byte) {
  /* White_Space + line terminators representable in one byte. */
  return byte == '\t' || byte == '\n' || byte == '\v' || byte == '\f' ||
         byte == '\r' || byte == ' ' || byte == 160;
}

char *qa_input_js_trim(const char *text) {
  const unsigned char *start = (const unsigned char *)text;
  while (*start != '\0' && qa_input_js_space(*start)) start++;
  size_t length = strlen((const char *)start);
  while (length > 0 && qa_input_js_space(start[length - 1])) length--;
  return qa_input_dup_range((const char *)start, length);
}

static double parse_radix_digits(const char *text, int radix) {
  if (*text == '\0') return NAN;
  double value = 0;
  for (const char *at = text; *at != '\0'; at++) {
    int digit;
    if (*at >= '0' && *at <= '9') digit = *at - '0';
    else if (*at >= 'a' && *at <= 'z') digit = *at - 'a' + 10;
    else if (*at >= 'A' && *at <= 'Z') digit = *at - 'A' + 10;
    else return NAN;
    if (digit >= radix) return NAN;
    value = value * radix + digit;
  }
  return value;
}

double qa_input_js_parse_number(const char *text) {
  char *trimmed = qa_input_js_trim(text);
  if (trimmed == NULL) return NAN;
  double result;
  if (trimmed[0] == '\0') {
    result = 0;
  } else if (strcmp(trimmed, "Infinity") == 0 || strcmp(trimmed, "+Infinity") == 0) {
    result = INFINITY;
  } else if (strcmp(trimmed, "-Infinity") == 0) {
    result = -INFINITY;
  } else if (trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
    result = parse_radix_digits(trimmed + 2, 16);
  } else if (trimmed[0] == '0' && (trimmed[1] == 'b' || trimmed[1] == 'B')) {
    result = parse_radix_digits(trimmed + 2, 2);
  } else if (trimmed[0] == '0' && (trimmed[1] == 'o' || trimmed[1] == 'O')) {
    result = parse_radix_digits(trimmed + 2, 8);
  } else {
    char *end = NULL;
    result = strtod(trimmed, &end);
    if (end == trimmed || *end != '\0') result = NAN;
  }
  free(trimmed);
  return result;
}

double qa_input_js_parse_int10(const char *text) {
  const unsigned char *at = (const unsigned char *)text;
  while (*at != '\0' && qa_input_js_space(*at)) at++;
  double sign = 1;
  if (*at == '-' || *at == '+') {
    if (*at == '-') sign = -1;
    at++;
  }
  if (*at < '0' || *at > '9') return NAN;
  double value = 0;
  while (*at >= '0' && *at <= '9') {
    value = value * 10 + (*at - '0');
    at++;
  }
  return sign * value;
}

bool qa_input_binding_copy(qa_input_binding *out, const qa_input_binding *in) {
  memset(out, 0, sizeof *out);
  out->input = in->input;
  out->target = in->target;
  out->input.axis = NULL;
  out->input.direction = NULL;
  out->target.action = NULL;
  out->target.text = NULL;
  if (in->input.axis != NULL) {
    out->input.axis = qa_input_dup(in->input.axis);
    if (out->input.axis == NULL) goto fail;
  }
  if (in->input.direction != NULL) {
    out->input.direction = qa_input_dup(in->input.direction);
    if (out->input.direction == NULL) goto fail;
  }
  if (in->target.action != NULL) {
    out->target.action = qa_input_dup(in->target.action);
    if (out->target.action == NULL) goto fail;
  }
  if (in->target.text != NULL) {
    out->target.text = qa_input_dup(in->target.text);
    if (out->target.text == NULL) goto fail;
  }
  return true;
fail:
  qa_input_binding_dispose(out);
  return false;
}

void qa_input_binding_dispose(qa_input_binding *binding) {
  free((void *)binding->input.axis);
  free((void *)binding->input.direction);
  free((void *)binding->target.action);
  free((void *)binding->target.text);
  memset(binding, 0, sizeof *binding);
}
