/* See include/qa/network/common/js_number.h. GPL-2.0-or-later. */
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void qa_net_js_number(double value, char *buffer, size_t capacity) {
  if (isnan(value)) { snprintf(buffer, capacity, "NaN"); return; }
  if (isinf(value)) { snprintf(buffer, capacity, value < 0 ? "-Infinity" : "Infinity"); return; }
  if (value == 0) { snprintf(buffer, capacity, "0"); return; }
  char scientific[40];
  for (int precision = 0; precision < 17; precision++) {
    snprintf(scientific, sizeof scientific, "%.*e", precision, value);
    if (strtod(scientific, NULL) == value) break;
  }
  const char *cursor = scientific;
  const char *sign = "";
  if (*cursor == '-') { sign = "-"; cursor++; }
  char digits[24];
  int k = 0;
  for (; *cursor && *cursor != 'e'; cursor++)
    if (*cursor != '.') digits[k++] = *cursor;
  int n = atoi(cursor + 1) + 1;
  while (k > 1 && digits[k - 1] == '0') k--;
  digits[k] = '\0';
  if (k <= n && n <= 21) {
    int used = snprintf(buffer, capacity, "%s%s", sign, digits);
    for (int i = k; i < n && (size_t)used + 1 < capacity; i++) buffer[used++] = '0';
    if ((size_t)used < capacity) buffer[used] = '\0';
  } else if (0 < n && n <= 21) {
    snprintf(buffer, capacity, "%s%.*s.%s", sign, n, digits, digits + n);
  } else if (-6 < n && n <= 0) {
    snprintf(buffer, capacity, "%s0.%0*d%s", sign, -n, 0, digits);
    if (n == 0) snprintf(buffer, capacity, "%s0.%s", sign, digits);
  } else if (k == 1) {
    snprintf(buffer, capacity, "%s%se%c%d", sign, digits, n - 1 < 0 ? '-' : '+', abs(n - 1));
  } else {
    snprintf(buffer, capacity, "%s%c.%se%c%d", sign, digits[0], digits + 1, n - 1 < 0 ? '-' : '+', abs(n - 1));
  }
}
void qa_net_json_number(double value, char *buffer, size_t capacity) {
  if (!isfinite(value)) snprintf(buffer, capacity, "null");
  else qa_net_js_number(value, buffer, capacity);
}
const char *qa_net_js_number_text(double value, char *buffer) {
  qa_net_js_number(value, buffer, 40);
  return buffer;
}
