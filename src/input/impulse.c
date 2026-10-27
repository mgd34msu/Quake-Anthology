/* Port of src/input/impulse.ts. GPL-2.0-or-later. */
#include "qa/input/impulse.h"

#include <string.h>

#include "qa/input/support.h"

static double quake_integer(const char *text) {
  const unsigned char *bytes = (const unsigned char *)text;
  size_t length = strlen(text);
  size_t offset = bytes[0] == '-' ? 1 : 0;
  double sign = offset == 1 ? -1 : 1;
  double value = 0;
  if (bytes[offset] == '0' && (bytes[offset + 1] == 'x' || bytes[offset + 1] == 'X')) {
    offset += 2;
    /* The hex path shifts a 32-bit JS bitwise value each step. */
    int32_t hex = 0;
    for (; offset < length; offset++) {
      int code = bytes[offset];
      if (code >= 48 && code <= 57) hex = (int32_t)((uint32_t)hex << 4) + code - 48;
      else if (code >= 97 && code <= 102) hex = (int32_t)((uint32_t)hex << 4) + code - 97 + 10;
      else if (code >= 65 && code <= 70) hex = (int32_t)((uint32_t)hex << 4) + code - 65 + 10;
      else break;
    }
    value = hex;
  } else if (bytes[offset] == '\'') {
    value = offset + 1 < length ? bytes[offset + 1] : 0;
  } else {
    for (; offset < length; offset++) {
      int code = bytes[offset];
      if (code < 48 || code > 57) break;
      value = value * 10 + (code - 48);
    }
  }
  return sign * value;
}

double qa_parse_impulse(const char *text, qa_command_dialect dialect) {
  switch (dialect) {
    case QA_COMMAND_DIALECT_Q1_NETQUAKE:
    case QA_COMMAND_DIALECT_Q1_QUAKEWORLD:
      return (double)(qa_input_to_int32(quake_integer(text)) & 255);
    case QA_COMMAND_DIALECT_Q2_CLASSIC:
      return (double)(qa_input_to_int32(qa_input_js_parse_int10(text)) & 255);
    case QA_COMMAND_DIALECT_Q2_RERELEASE:
    case QA_COMMAND_DIALECT_Q3:
      return qa_input_js_parse_number(text);
  }
  return 0;
}
