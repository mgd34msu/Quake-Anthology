/* Port of src/content/q2/foundation/start-items.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/start_items.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

static char *dup_range(const char *s, size_t n) {
  char *p = malloc(n + 1);
  if (p) { memcpy(p, s, n); p[n] = '\0'; }
  return p;
}

qa_q2_start_item *qa_q2_parse_start_items(const char *expression, size_t *out_count) {
  qa_q2_start_item *list = NULL;
  size_t count = 0, cap = 0;
  const char *seg = expression;
  for (;;) {
    const char *semi = strchr(seg, ';');
    size_t len = semi ? (size_t)(semi - seg) : strlen(seg);
    /* TS trim() then skip empty. */
    size_t b = 0, e = len;
    while (b < e && isspace((unsigned char)seg[b])) b++;
    while (e > b && isspace((unsigned char)seg[e - 1])) e--;
    if (e > b) {
      char *value = dup_range(seg + b, e - b);
      if (!value) qa_range_error("out of memory");
      size_t space = 0;
      while (space < strlen(value) && !isspace((unsigned char)value[space])) space++;
      bool has_space = space < strlen(value);
      char *classname = dup_range(value, has_space ? space : strlen(value));
      double count_value = 1;
      bool ok = classname != NULL;
      if (ok && has_space) {
        const char *p = value + space + 1;
        while (isspace((unsigned char)*p)) p++;
        /* Number.parseInt(...,10) prefix parse. */
        if (!(*p == '+' || *p == '-' || isdigit((unsigned char)*p))) ok = false;
        else {
          char *end = NULL;
          long long v = strtoll(p, &end, 10);
          if (end == p) ok = false;
          else count_value = (double)v;
        }
      }
      if (ok) {
        for (const char *p = classname; *p; p++)
          if (!isalnum((unsigned char)*p) && *p != '_') { ok = false; break; }
        if (classname[0] == '\0') ok = false;
        if (count_value != (double)(int64_t)count_value) ok = false;
      }
      if (!ok) qa_range_error("Invalid Q2 starting item: %s", value);
      free(value);
      if (count == cap) {
        size_t ncap = cap ? cap * 2 : 8;
        qa_q2_start_item *next = realloc(list, ncap * sizeof *next);
        if (!next) qa_range_error("out of memory");
        list = next; cap = ncap;
      }
      list[count++] = (qa_q2_start_item){classname, count_value};
    }
    if (!semi) break;
    seg = semi + 1;
  }
  *out_count = count;
  return list;
}

void qa_q2_start_items_free(qa_q2_start_item *items, size_t count) {
  for (size_t i = 0; i < count; i++) free(items[i].classname);
  free(items);
}
