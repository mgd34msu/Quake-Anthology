/* JavaScript case mapping and localeCompare; see include/qa/foundation/js_case.h.
 * GPL-2.0-or-later. */
#include "qa/foundation/js_case.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

typedef struct js_case_pair {
  uint32_t from, to;
} js_case_pair;
typedef struct js_case_multi {
  uint32_t from;
  uint32_t count;
  uint32_t to[3];
} js_case_multi;

#include "js_case_tables.inc"

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

static const js_case_pair *find_pair(const js_case_pair *table, size_t count, uint32_t cp) {
  size_t low = 0, high = count;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (table[mid].from == cp) return &table[mid];
    if (table[mid].from < cp) low = mid + 1;
    else high = mid;
  }
  return NULL;
}

static const js_case_multi *find_multi(const js_case_multi *table, size_t count, uint32_t cp) {
  for (size_t i = 0; i < count; i++) {
    if (table[i].from == cp) return &table[i];
  }
  return NULL;
}

static bool in_ranges(const js_case_pair *ranges, size_t count, uint32_t cp) {
  size_t low = 0, high = count;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    if (cp < ranges[mid].from) high = mid;
    else if (cp > ranges[mid].to) low = mid + 1;
    else return true;
  }
  return false;
}

/* One decoded unit: a code point, or a byte that is not valid UTF-8 (raw). */
typedef struct unit {
  uint32_t cp;
  bool raw;
} unit;

static size_t decode_one(const unsigned char *s, unit *out) {
  unsigned char c = s[0];
  out->raw = false;
  if (c < 0x80) { out->cp = c; return 1; }
  size_t need;
  uint32_t cp, min;
  if (c >= 0xc2 && c <= 0xdf) { need = 1; cp = c & 0x1f; min = 0x80; }
  else if (c >= 0xe0 && c <= 0xef) { need = 2; cp = c & 0x0f; min = 0x800; }
  else if (c >= 0xf0 && c <= 0xf4) { need = 3; cp = c & 0x07; min = 0x10000; }
  else goto raw;
  for (size_t i = 1; i <= need; i++) {
    if ((s[i] & 0xc0) != 0x80) goto raw;
    cp = (cp << 6) | (s[i] & 0x3f);
  }
  if (cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) goto raw;
  out->cp = cp;
  return need + 1;
raw:
  out->cp = c;
  out->raw = true;
  return 1;
}

static unit *decode_all(const char *text, size_t *count) {
  size_t length = strlen(text);
  unit *units = malloc((length + 1) * sizeof *units);
  if (units == NULL) return NULL;
  size_t n = 0;
  for (size_t at = 0; at < length;) at += decode_one((const unsigned char *)text + at, &units[n++]);
  *count = n;
  return units;
}

static size_t encode(uint32_t cp, char *out) {
  if (cp < 0x80) { out[0] = (char)cp; return 1; }
  if (cp < 0x800) { out[0] = (char)(0xc0 | (cp >> 6)); out[1] = (char)(0x80 | (cp & 0x3f)); return 2; }
  if (cp < 0x10000) {
    out[0] = (char)(0xe0 | (cp >> 12)); out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[2] = (char)(0x80 | (cp & 0x3f));
    return 3;
  }
  out[0] = (char)(0xf0 | (cp >> 18)); out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3f)); out[3] = (char)(0x80 | (cp & 0x3f));
  return 4;
}

static bool cased(const unit *u) { return !u->raw && in_ranges(js_cased_ranges, COUNT(js_cased_ranges), u->cp); }
static bool ignorable(const unit *u) {
  return !u->raw && in_ranges(js_case_ignorable_ranges, COUNT(js_case_ignorable_ranges), u->cp);
}

/* Unicode Final_Sigma, checking Case_Ignorable before Cased as ICU does. */
static bool final_sigma(const unit *units, size_t count, size_t at) {
  bool before = false;
  for (size_t i = at; i-- > 0;) {
    if (ignorable(&units[i])) continue;
    before = cased(&units[i]);
    break;
  }
  if (!before) return false;
  for (size_t i = at + 1; i < count; i++) {
    if (ignorable(&units[i])) continue;
    return !cased(&units[i]);
  }
  return true;
}

static char *map_case(const char *text, bool upper) {
  size_t count;
  unit *units = decode_all(text, &count);
  if (units == NULL) return NULL;
  char *out = malloc(strlen(text) * 3 + 1);
  if (out == NULL) { free(units); return NULL; }
  size_t length = 0;
  for (size_t i = 0; i < count; i++) {
    const unit *u = &units[i];
    if (u->raw) { out[length++] = (char)u->cp; continue; }
    uint32_t cp = u->cp;
    if (!upper && cp == 0x3a3) {
      length += encode(final_sigma(units, count, i) ? 0x3c2 : 0x3c3, out + length);
      continue;
    }
    const js_case_multi *multi = upper ? find_multi(js_upper_multi, COUNT(js_upper_multi), cp)
                                       : find_multi(js_lower_multi, COUNT(js_lower_multi), cp);
    if (multi != NULL) {
      for (uint32_t k = 0; k < multi->count; k++) length += encode(multi->to[k], out + length);
      continue;
    }
    const js_case_pair *pair = upper ? find_pair(js_upper_pairs, COUNT(js_upper_pairs), cp)
                                     : find_pair(js_lower_pairs, COUNT(js_lower_pairs), cp);
    length += encode(pair != NULL ? pair->to : cp, out + length);
  }
  out[length] = '\0';
  free(units);
  return out;
}

char *qa_js_to_lower_case(const char *text) { return map_case(text, false); }
char *qa_js_to_upper_case(const char *text) { return map_case(text, true); }

static char *lower_or_abort(const char *text) {
  char *lowered = qa_js_to_lower_case(text);
  if (lowered == NULL) qa_range_error("Out of memory lowering text");
  return lowered;
}

bool qa_js_equals_ignore_case(const char *a, const char *b) {
  char *x = lower_or_abort(a), *y = lower_or_abort(b);
  bool equal = strcmp(x, y) == 0;
  free(x);
  free(y);
  return equal;
}

bool qa_js_ends_with_ignore_case(const char *text, const char *suffix) {
  char *x = lower_or_abort(text), *y = lower_or_abort(suffix);
  size_t a = strlen(x), b = strlen(y);
  bool match = b <= a && memcmp(x + a - b, y, b) == 0;
  free(x);
  free(y);
  return match;
}

bool qa_js_starts_with_ignore_case(const char *text, const char *prefix) {
  char *x = lower_or_abort(text), *y = lower_or_abort(prefix);
  bool match = strncmp(x, y, strlen(y)) == 0;
  free(x);
  free(y);
  return match;
}

char *qa_js_latin1_to_lower_case(const char *text) {
  size_t length = strlen(text);
  char *out = malloc(length + 1);
  if (out == NULL) return NULL;
  for (size_t i = 0; i <= length; i++) out[i] = (char)qa_js_latin1_lower((unsigned char)text[i]);
  return out;
}

bool qa_js_latin1_equals_ignore_case(const char *a, const char *b) {
  const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
  for (;; x++, y++) {
    if (qa_js_latin1_lower(*x) != qa_js_latin1_lower(*y)) return false;
    if (*x == '\0') return true;
  }
}

/* ---- localeCompare -------------------------------------------------------- */

/* bun's default collator order for ASCII, primary level (probed under bun):
 * TAB LF VT FF CR SP _ - , ; : ! ? . ' " ( ) [ ] { } @ * / \ & # % ` ^ + < = > | ~ $
 * 0-9, then letters with a/A sharing a primary weight. Other controls are ignorable. */
static const char ASCII_ORDER[] = "\t\n\v\f\r _-,;:!?.'\"()[]{}@*/\\&#%`^+<=>|~$0123456789";

static uint32_t ascii_primary(unsigned char c) {
  const char *at = c != 0 ? strchr(ASCII_ORDER, c) : NULL;
  if (at != NULL) return (uint32_t)(at - ASCII_ORDER) + 1;
  if (c >= 'a' && c <= 'z') return 100 + (uint32_t)(c - 'a');
  if (c >= 'A' && c <= 'Z') return 100 + (uint32_t)(c - 'A');
  return 0; /* ignorable */
}

#define DIGIT_PRIMARY 39u /* position of '0' in ASCII_ORDER, plus one */

typedef struct element {
  uint32_t primary; /* 0: ignorable */
  uint32_t tertiary;
  const char *digits; /* numeric run (leading zeros stripped), or NULL */
  size_t digit_count;
} element;

static element *collation_elements(const char *text, bool numeric, size_t *count) {
  size_t length = strlen(text);
  element *out = malloc((length + 1) * sizeof *out);
  if (out == NULL) qa_range_error("Out of memory comparing text");
  size_t n = 0;
  const unsigned char *s = (const unsigned char *)text;
  for (size_t at = 0; at < length;) {
    unit u;
    size_t used = decode_one(s + at, &u);
    element e = {0, 0, NULL, 0};
    if (numeric && u.cp >= '0' && u.cp <= '9' && !u.raw) {
      size_t end = at;
      while (s[end] >= '0' && s[end] <= '9') end++;
      size_t first = at;
      while (first + 1 < end && s[first] == '0') first++;
      if (s[first] == '0') first = end; /* all zeros: value 0, no digits */
      e.primary = DIGIT_PRIMARY;
      e.digits = (const char *)s + first;
      e.digit_count = end - first;
      out[n++] = e;
      at = end;
      continue;
    }
    if (u.cp < 0x80 && !u.raw) {
      e.primary = ascii_primary((unsigned char)u.cp);
      e.tertiary = u.cp >= 'A' && u.cp <= 'Z';
    } else {
      e.primary = 1000 + u.cp; /* approximation outside ASCII */
    }
    if (e.primary != 0) out[n++] = e;
    at += used;
  }
  *count = n;
  return out;
}

static int compare_primary(const element *a, const element *b) {
  if (a->digits != NULL && b->digits != NULL) {
    if (a->digit_count != b->digit_count) return a->digit_count < b->digit_count ? -1 : 1;
    int order = memcmp(a->digits, b->digits, a->digit_count);
    return order < 0 ? -1 : order > 0;
  }
  if (a->primary != b->primary) return a->primary < b->primary ? -1 : 1;
  return 0;
}

static int collate(const char *a, const char *b, bool tertiary, bool numeric) {
  size_t na, nb;
  element *x = collation_elements(a, numeric, &na), *y = collation_elements(b, numeric, &nb);
  int result = 0;
  size_t n = na < nb ? na : nb;
  for (size_t i = 0; i < n && result == 0; i++) result = compare_primary(&x[i], &y[i]);
  if (result == 0 && na != nb) result = na < nb ? -1 : 1;
  for (size_t i = 0; tertiary && i < n && result == 0; i++) {
    if (x[i].tertiary != y[i].tertiary) result = x[i].tertiary < y[i].tertiary ? -1 : 1;
  }
  free(x);
  free(y);
  return result;
}

int qa_js_locale_compare(const char *a, const char *b) { return collate(a, b, true, false); }
int qa_js_locale_compare_base(const char *a, const char *b) { return collate(a, b, false, false); }
int qa_js_locale_compare_numeric(const char *a, const char *b) { return collate(a, b, true, true); }
