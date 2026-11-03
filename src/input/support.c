/* See include/qa/input/support.h. GPL-2.0-or-later. */
#include "qa/input/support.h"
#include "qa/network/common/js_number.h"

#include "qa/foundation/support.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"

noreturn void qa_input_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
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
  /* String(value): the shared ECMAScript Number::toString. A %g search printed
   * 0.00001 as "1e-5" where JS writes "0.00001". */
  char buffer[40];
  qa_net_js_number(value, buffer, sizeof buffer);
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

double qa_input_js_parse_number(const char *text) { return qa_js_string_to_number(text); }

double qa_input_js_parse_int10(const char *text) { return qa_js_parse_int(text, 10); }

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
