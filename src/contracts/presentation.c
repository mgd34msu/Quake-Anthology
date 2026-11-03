/* Port of src/contracts/presentation.ts (runtime helpers). GPL-2.0-or-later. */
#include "qa/contracts/presentation.h"
#include "qa/network/common/js_number.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* JSON.stringify number formatting: non-finite numbers are null, -0 is 0. */
static void format_json_number(double value, char *buffer, size_t size) {
  qa_net_json_number(value, buffer, size);
}

static size_t json_escaped_length(const char *text) {
  size_t length = 0;
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    if (*p == '"' || *p == '\\' || *p == '\b' || *p == '\f' || *p == '\n' ||
        *p == '\r' || *p == '\t') {
      length += 2;
    } else if (*p < 0x20) {
      length += 6; /* \u00xx */
    } else {
      length += 1;
    }
  }
  return length;
}

static char *write_json_escaped(char *cursor, const char *text) {
  static const char hex_digits[] = "0123456789abcdef";
  for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; p++) {
    switch (*p) {
      case '"': *cursor++ = '\\'; *cursor++ = '"'; break;
      case '\\': *cursor++ = '\\'; *cursor++ = '\\'; break;
      case '\b': *cursor++ = '\\'; *cursor++ = 'b'; break;
      case '\f': *cursor++ = '\\'; *cursor++ = 'f'; break;
      case '\n': *cursor++ = '\\'; *cursor++ = 'n'; break;
      case '\r': *cursor++ = '\\'; *cursor++ = 'r'; break;
      case '\t': *cursor++ = '\\'; *cursor++ = 't'; break;
      default:
        if (*p < 0x20) {
          *cursor++ = '\\';
          *cursor++ = 'u';
          *cursor++ = '0';
          *cursor++ = '0';
          *cursor++ = hex_digits[*p >> 4];
          *cursor++ = hex_digits[*p & 0xf];
        } else {
          *cursor++ = (char)*p;
        }
        break;
    }
  }
  return cursor;
}

char *qa_presentation_owner_key(const qa_presentation_owner *owner) {
  if (owner == NULL) { /* TS undefined */
    char *out = (char *)malloc(sizeof "primary");
    if (out != NULL) memcpy(out, "primary", sizeof "primary");
    return out;
  }
  char number[40];
  format_json_number(owner->generation, number, sizeof number);
  /* [" provider " , number ] */
  size_t length = 2 + json_escaped_length(owner->provider) + 2 + strlen(number) + 1;
  char *out = (char *)malloc(length + 1);
  if (out == NULL) return NULL;
  char *cursor = out;
  *cursor++ = '[';
  *cursor++ = '"';
  cursor = write_json_escaped(cursor, owner->provider);
  *cursor++ = '"';
  *cursor++ = ',';
  size_t number_length = strlen(number);
  memcpy(cursor, number, number_length);
  cursor += number_length;
  *cursor++ = ']';
  *cursor = '\0';
  return out;
}

bool qa_same_presentation_owner(const qa_presentation_owner *left,
                                qa_presentation_owner right) {
  /* TS: left?.provider === right.provider short-circuits &&, so an
   * undefined left returns false and never reads left.generation. */
  if (left == NULL) return false;
  bool same_provider = (left->provider == NULL && right.provider == NULL) ||
      (left->provider != NULL && right.provider != NULL &&
       strcmp(left->provider, right.provider) == 0);
  return same_provider && left->generation == right.generation;
}
