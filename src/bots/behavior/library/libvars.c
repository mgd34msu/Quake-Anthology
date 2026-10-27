/* See header. GPL-2.0-or-later. */
#include "qa/bots/behavior/library/libvars.h"
#include "qa/foundation/support.h"
#include <stdlib.h>
#include <string.h>
static float parse_value(const char *t) {
  int64_t den = 0;
  float val = 0.0f;
  size_t len = strlen(t);
  for (size_t i = 0; i < len; i++) {
    int byte = (uint8_t)t[i];
    if (byte < 48 || byte > 57) {
      if (den != 0 || byte != 46)
        return 0.0f;
      den = 10;
      i++;
      if (i == len)
        return 0.0f;
      byte = (uint8_t)t[i];
    }
    if (den != 0) {
      int sb = byte < 128 ? byte : byte - 256;
      val = (float)((double)val + (double)(float)((double)(sb - 48) / (double)den));
      if (den * 10 > 2147483647LL)
        qa_range_error(
            "LibVarStringValue: fractional denominator would overflow the source signed int");
      den *= 10;
    } else
      val = (float)((double)val * 10.0 + (double)(byte - 48));
  }
  return val;
}
float qa_lib_var_string_value(const char *text) { return parse_value(text ? text : ""); }
static int up(int c) { return (c >= 97 && c <= 122) ? c - 32 : c; }
static bool same(const char *a, const char *b) {
  while (*a && *b) {
    if (up((uint8_t)*a) != up((uint8_t)*b))
      return false;
    a++;
    b++;
  }
  return *a == *b;
}
static char *dup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p)
    memcpy(p, s, n);
  return p;
}
void qa_bot_lib_vars_init(qa_bot_lib_vars *v) { v->head = NULL; }
void qa_bot_lib_vars_clear(qa_bot_lib_vars *v) {
  while (v->head) {
    qa_bot_lib_var *n = v->head->next;
    free(v->head->name);
    free(v->head->string);
    free(v->head);
    v->head = n;
  }
}
void qa_bot_lib_vars_destroy(qa_bot_lib_vars *v) { qa_bot_lib_vars_clear(v); }
qa_bot_lib_var *qa_bot_lib_vars_get(qa_bot_lib_vars *v, const char *name) {
  for (qa_bot_lib_var *r = v->head; r; r = r->next)
    if (same(r->name, name))
      return r;
  return NULL;
}
const char *qa_bot_lib_vars_get_string(qa_bot_lib_vars *v, const char *n) {
  qa_bot_lib_var *r = qa_bot_lib_vars_get(v, n);
  return r ? r->string : "";
}
float qa_bot_lib_vars_get_value(qa_bot_lib_vars *v, const char *n) {
  qa_bot_lib_var *r = qa_bot_lib_vars_get(v, n);
  return r ? r->value : 0.0f;
}
static qa_bot_lib_var *alloc_var(qa_bot_lib_vars *v, const char *name) {
  qa_bot_lib_var *r = calloc(1, sizeof *r);
  if (!r)
    qa_range_error("Bot library variable pointer IDs exhausted");
  r->name = dup(name);
  r->string = dup("");
  r->next = v->head;
  v->head = r;
  return r;
}
static void assign(qa_bot_lib_var *r, const char *text) {
  free(r->string);
  r->string = dup(text);
  r->value = parse_value(text);
  r->modified = true;
}
qa_bot_lib_var *qa_bot_lib_vars_get_or_create(qa_bot_lib_vars *v, const char *n, const char *d) {
  qa_bot_lib_var *r = qa_bot_lib_vars_get(v, n);
  if (r)
    return r;
  r = alloc_var(v, n);
  assign(r, d);
  return r;
}
const char *qa_bot_lib_vars_string(qa_bot_lib_vars *v, const char *n, const char *d) {
  return qa_bot_lib_vars_get_or_create(v, n, d)->string;
}
float qa_bot_lib_vars_value(qa_bot_lib_vars *v, const char *n, const char *d) {
  return qa_bot_lib_vars_get_or_create(v, n, d)->value;
}
void qa_bot_lib_vars_set(qa_bot_lib_vars *v, const char *n, const char *val) {
  qa_bot_lib_var *r = qa_bot_lib_vars_get(v, n);
  if (!r)
    r = alloc_var(v, n);
  assign(r, val);
}
bool qa_bot_lib_vars_changed(qa_bot_lib_vars *v, const char *n) {
  qa_bot_lib_var *r = qa_bot_lib_vars_get(v, n);
  return r ? r->modified : false;
}
void qa_bot_lib_vars_set_not_modified(qa_bot_lib_vars *v, const char *n) {
  qa_bot_lib_var *r = qa_bot_lib_vars_get(v, n);
  if (r)
    r->modified = false;
}
