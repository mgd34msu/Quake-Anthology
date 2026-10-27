/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/character.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
void qa_bot_character_library_init(qa_bot_character_library *l) { memset(l, 0, sizeof *l); }
void qa_bot_character_library_destroy(qa_bot_character_library *l) {
  for (int i = 0; i <= QA_MAX_BOT_CHARACTERS; i++)
    for (int j = 0; j < 49; j++)
      free(l->records[i].strings[j]);
}
int64_t qa_bot_character_allocate(qa_bot_character_library *l) {
  for (int i = 1; i <= QA_MAX_BOT_CHARACTERS; i++)
    if (!l->records[i].used) {
      l->records[i].used = true;
      return i;
    }
  return 0;
}
void qa_bot_character_free(qa_bot_character_library *l, int64_t h) {
  if (h >= 1 && h <= QA_MAX_BOT_CHARACTERS)
    l->records[h].used = false;
}
static qa_bot_character_record *rec(qa_bot_character_library *l, int64_t h, int64_t idx) {
  if (h < 1 || h > QA_MAX_BOT_CHARACTERS || !l->records[h].used)
    qa_range_error("invalid bot character handle");
  if (idx < 0 || idx >= 49)
    qa_range_error("invalid bot characteristic index");
  return &l->records[h];
}
void qa_bot_character_set_float(qa_bot_character_library *l, int64_t h, int64_t i, double v) {
  qa_bot_character_record *r = rec(l, h, i);
  r->values[i] = (float)v;
  r->is_string[i] = false;
}
void qa_bot_character_set_string(qa_bot_character_library *l, int64_t h, int64_t i, const char *v) {
  qa_bot_character_record *r = rec(l, h, i);
  free(r->strings[i]);
  size_t n = strlen(v) + 1;
  r->strings[i] = malloc(n);
  if (r->strings[i])
    memcpy(r->strings[i], v, n);
  r->is_string[i] = true;
}
double qa_bot_character_get_float(qa_bot_character_library *l, int64_t h, int64_t i) {
  return rec(l, h, i)->values[i];
}
const char *qa_bot_character_get_string(qa_bot_character_library *l, int64_t h, int64_t i) {
  const char *s = rec(l, h, i)->strings[i];
  return s ? s : "";
}
double qa_bot_character_bounded_float(qa_bot_character_library *l, int64_t h, int64_t i, double mn,
                                      double mx) {
  if (mn > mx)
    qa_range_error("invalid characteristic bounds");
  double v = qa_bot_character_get_float(l, h, i);
  return v < mn ? mn : v > mx ? mx : v;
}
double qa_bot_character_bounded_integer(qa_bot_character_library *l, int64_t h, int64_t i,
                                        double mn, double mx) {
  return trunc(qa_bot_character_bounded_float(l, h, i, mn, mx));
}
