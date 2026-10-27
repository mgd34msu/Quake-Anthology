/* Port of src/content/q3/base/game/shader-remaps.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/shader_remaps.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/format.h"
#include "qa/foundation/support.h"
enum { MAX_QPATH = 64, MAX_SHADER_REMAPS = 128, ENTRY_BUFFER_BYTES = 64 * 2 + 5, STATE_BUFFER_BYTES = 1024 * 4 };
static char *dupstr(const char *v) { size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
static const char *quake_path(const char *v) {
  if (strlen(v) >= MAX_QPATH) qa_range_error("shader remap paths must fit MAX_QPATH including the terminator");
  return v;
}
static int fold(int b) { return b >= 97 && b <= 122 ? b - 32 : b; }
static int path_equal(const char *a, const char *b) {
  if (strlen(a) != strlen(b)) return 0;
  for (size_t i = 0; a[i]; i++) if (fold((unsigned char)a[i]) != fold((unsigned char)b[i])) return 0;
  return 1;
}
static double stored_time(double v) { float s = (float)v; if (!isfinite(s) || fabs(s) > 2147483647) qa_range_error("shader remap time is outside the source formatter's safe int-cast range"); return s; }
void qa_q3_shader_remap_registry_init(qa_q3_shader_remap_registry *r, void *ctx, void (*print)(void *, const char *)) {
  r->remaps = NULL; r->count = 0; r->cap = 0; r->print_ctx = ctx; r->print = print;
}
void qa_q3_shader_remap_registry_destroy(qa_q3_shader_remap_registry *r) {
  for (size_t i = 0; i < r->count; i++) { free(r->remaps[i].old_name); free(r->remaps[i].new_name); } free(r->remaps); r->remaps = NULL; r->count = r->cap = 0;
}
void qa_q3_shader_remap_registry_add(qa_q3_shader_remap_registry *r, const char *old_name, const char *new_name, double time_offset) {
  const char *o = quake_path(old_name), *n = quake_path(new_name); double t = stored_time(time_offset);
  for (size_t i = 0; i < r->count; i++) if (path_equal(o, r->remaps[i].old_name)) { free(r->remaps[i].new_name); r->remaps[i].new_name = dupstr(n); r->remaps[i].time_offset = t; return; }
  if (r->count < MAX_SHADER_REMAPS) {
    if (r->count == r->cap) { size_t cap = r->cap ? r->cap * 2 : 8; void *m = realloc(r->remaps, cap * sizeof *r->remaps); if (!m) qa_range_error("out of memory"); r->remaps = m; r->cap = cap; }
    r->remaps[r->count].old_name = dupstr(o); r->remaps[r->count].new_name = dupstr(n); r->remaps[r->count].time_offset = t; r->count++;
  }
}
char *qa_q3_shader_remap_registry_build_config(qa_q3_shader_remap_registry *r) {
  char *state = calloc(1, 1); if (!state) return NULL; size_t slen = 0;
  for (size_t i = 0; i < r->count; i++) {
    qa_q3_game_format_arg args[3] = {qa_q3_game_format_string(r->remaps[i].old_name), qa_q3_game_format_string(r->remaps[i].new_name), qa_q3_game_format_number(r->remaps[i].time_offset)};
    char *formatted = qa_q3_game_format("%s=%s:%5.2f@", args, 3, 32000); if (!formatted) { free(state); return NULL; }
    size_t flen = strlen(formatted);
    if (flen >= ENTRY_BUFFER_BYTES && r->print) {
      qa_q3_game_format_arg pa[2] = {qa_q3_game_format_number((double)flen), qa_q3_game_format_number(ENTRY_BUFFER_BYTES)};
      char *msg = qa_q3_game_format("Com_sprintf: overflow of %i in %i\n", pa, 2, 32000); if (msg) { r->print(r->print_ctx, msg); free(msg); }
    }
    size_t entry_len = flen < ENTRY_BUFFER_BYTES - 1 ? flen : ENTRY_BUFFER_BYTES - 1;
    long writable = (long)STATE_BUFFER_BYTES - (long)slen - 1;
    if (writable > 0) { size_t add = entry_len < (size_t)writable ? entry_len : (size_t)writable; char *ns = realloc(state, slen + add + 1); if (!ns) { free(formatted); free(state); return NULL; } state = ns; memcpy(state + slen, formatted, add); slen += add; state[slen] = 0; }
    free(formatted);
  }
  return state;
}
qa_q3_shader_remap_state qa_q3_shader_remap_registry_capture(qa_q3_shader_remap_registry *r) {
  qa_q3_shader_remap_state s; s.count = r->count; s.remaps = malloc((r->count ? r->count : 1) * sizeof *s.remaps);
  if (s.remaps) for (size_t i = 0; i < r->count; i++) { s.remaps[i].old_name = dupstr(r->remaps[i].old_name); s.remaps[i].new_name = dupstr(r->remaps[i].new_name); s.remaps[i].time_offset = r->remaps[i].time_offset; }
  return s;
}
void qa_q3_shader_remap_state_destroy(qa_q3_shader_remap_state *s) {
  for (size_t i = 0; i < s->count; i++) { free(s->remaps[i].old_name); free(s->remaps[i].new_name); } free(s->remaps); s->remaps = NULL; s->count = 0;
}
void qa_q3_shader_remap_registry_restore(qa_q3_shader_remap_registry *r, const qa_save_value *value) {
  qa_save_reader reader; qa_save_reader_init(&reader, value, "q3.remaps"); size_t n = qa_save_reader_list_count(&reader);
  qa_q3_shader_remap *list = calloc(n ? n : 1, sizeof *list); if (!list) qa_range_error("out of memory");
  for (size_t i = 0; i < n; i++) {
    qa_save_reader entry, f; qa_save_reader_item(&reader, i, &entry);
    qa_save_reader_field(&entry, "oldName", &f); const char *o = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
    qa_save_reader_field(&entry, "newName", &f); const char *nn = qa_save_reader_string(&f); qa_save_reader_destroy(&f);
    qa_save_reader_field(&entry, "timeOffset", &f); double t = qa_save_reader_number(&f); qa_save_reader_destroy(&f);
    list[i].old_name = dupstr(quake_path(o)); list[i].new_name = dupstr(quake_path(nn)); list[i].time_offset = stored_time(t);
    qa_save_reader_destroy(&entry);
  }
  int bad = n > MAX_SHADER_REMAPS;
  for (size_t i = 0; i < n && !bad; i++) for (size_t j = 0; j < i; j++) if (path_equal(list[j].old_name, list[i].old_name)) bad = 1;
  if (bad) qa_save_reader_fail(&reader, "invalid shader remap table");
  qa_q3_shader_remap_registry_destroy(r); r->remaps = list; r->count = n; r->cap = n; qa_save_reader_destroy(&reader);
}
