/* Port of src/app/bootstrap/q3-client/status.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/q3_client/status.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static char *xstrdup(const char *s) { char *p = strdup(s != NULL ? s : ""); if (p == NULL) qa_range_error("Out of memory"); return p; }
static bool name_is_status(const char *name) {
  const char *target = "cg_drawstatus";
  if (name == NULL) return false;
  for (size_t i = 0; target[i] != '\0'; i++)
    if (tolower((unsigned char)name[i]) != target[i]) return false;
  return name[strlen(target)] == '\0';
}
static void copy_snapshot(const qa_cvar_snapshot *in, qa_cvar_snapshot *out) {
  *out = *in;
  out->name = xstrdup(in->name);
  out->value = xstrdup(in->value);
  out->reset_value = xstrdup(in->reset_value);
  out->latched_value = in->latched_value != NULL ? xstrdup(in->latched_value) : NULL;
}
void qa_app_effective_status_cvar(const qa_cvar_snapshot *value, bool visible, qa_cvar_snapshot *out) {
  copy_snapshot(value, out);
  if (!name_is_status(value->name) || visible) return;
  free(out->value);
  out->value = xstrdup("0");
  out->numeric_value = 0;
  out->integer_value = 0;
}
struct qa_app_cgame_status_cvars {
  qa_app_qvm_cvar_services source;
  bool (*visible)(void *ctx); void *visible_ctx;
  bool has_previous;
  qa_cvar_snapshot previous_source;
  bool previous_visible;
  double revision;
};
static bool read_masked(qa_app_cgame_status_cvars *c, bool found, qa_cvar_snapshot *value) {
  if (!found || !name_is_status(value->name)) return found;
  bool permission = c->visible(c->visible_ctx);
  bool changed = !c->has_previous
    || c->previous_source.modification_count != value->modification_count
    || c->previous_visible != permission
    || strcmp(c->previous_source.value, value->value) != 0
    || c->previous_source.numeric_value != value->numeric_value
    || c->previous_source.integer_value != value->integer_value;
  if (changed) {
    if (c->revision == 0x7fffffff) qa_range_error("Cgame status cvar version exhausted");
    c->revision++;
    if (c->has_previous) qa_cvar_read_free(&c->previous_source);
    copy_snapshot(value, &c->previous_source);
    c->has_previous = true;
    c->previous_visible = permission;
  }
  qa_cvar_snapshot masked;
  qa_app_effective_status_cvar(value, permission, &masked);
  qa_cvar_read_free(value);
  *value = masked;
  value->modification_count = c->revision;
  return true;
}
static bool w_get(void *ctx, const char *n, qa_cvar_snapshot *o) {
  qa_app_cgame_status_cvars *c = ctx;
  return read_masked(c, c->source.get(c->source.ctx, n, o), o);
}
static bool w_read_vm(void *ctx, double h, qa_cvar_snapshot *o) {
  qa_app_cgame_status_cvars *c = ctx;
  return read_masked(c, c->source.read_vm(c->source.ctx, h, o), o);
}
static bool w_set(void *ctx, const char *n, const char *v, bool f, qa_cvar_snapshot *o) {
  qa_app_cgame_status_cvars *c = ctx; return c->source.set(c->source.ctx, n, v, f, o);
}
static bool w_set_value(void *ctx, const char *n, double v, qa_cvar_snapshot *o) {
  qa_app_cgame_status_cvars *c = ctx; return c->source.set_value(c->source.ctx, n, v, o);
}
static bool w_reset(void *ctx, const char *n, bool f, qa_cvar_snapshot *o) {
  qa_app_cgame_status_cvars *c = ctx; return c->source.reset(c->source.ctx, n, f, o);
}
static bool w_register(void *ctx, const char *n, const char *v, bool hf, double f, qa_cvar_snapshot *o) {
  qa_app_cgame_status_cvars *c = ctx; return c->source.register_cvar(c->source.ctx, n, v, hf, f, o);
}
static double w_bind(void *ctx, const char *n, const char *v, double f) {
  qa_app_cgame_status_cvars *c = ctx; return c->source.bind_vm(c->source.ctx, n, v, f);
}
static char *w_info(void *ctx, double f, double m) {
  qa_app_cgame_status_cvars *c = ctx; return c->source.info_string(c->source.ctx, f, m);
}
qa_app_cgame_status_cvars *qa_app_cgame_status_cvars_create(
    qa_app_qvm_cvar_services source, bool (*visible)(void *ctx), void *visible_ctx) {
  qa_app_cgame_status_cvars *c = calloc(1, sizeof *c);
  if (c == NULL) qa_range_error("Out of memory");
  c->source = source;
  c->visible = visible;
  c->visible_ctx = visible_ctx;
  return c;
}
void qa_app_cgame_status_cvars_destroy(qa_app_cgame_status_cvars *cvars) {
  if (cvars == NULL) return;
  if (cvars->has_previous) qa_cvar_read_free(&cvars->previous_source);
  free(cvars);
}
qa_app_qvm_cvar_services qa_app_cgame_status_cvars_services(qa_app_cgame_status_cvars *cvars) {
  return (qa_app_qvm_cvar_services){cvars, w_get, w_set, w_set_value, w_reset, w_register, w_bind, w_read_vm, w_info};
}
typedef struct status_record { int32_t pointer; qa_qvm_memory *memory; int32_t handle; } status_record;
struct qa_app_cgame_status_view {
  qa_app_cgame_status_cvars *wrapper;
  qa_app_qvm_cvar_services cvars;
  status_record *records;
  size_t record_count, record_capacity;
  uint8_t refresh_words[8];
};
qa_app_cgame_status_view *qa_app_cgame_status_view_create(
    qa_app_qvm_cvar_services source, bool (*visible)(void *ctx), void *visible_ctx) {
  qa_app_cgame_status_view *v = calloc(1, sizeof *v);
  if (v == NULL) qa_range_error("Out of memory");
  v->wrapper = qa_app_cgame_status_cvars_create(source, visible, visible_ctx);
  v->cvars = qa_app_cgame_status_cvars_services(v->wrapper);
  return v;
}
void qa_app_cgame_status_view_destroy(qa_app_cgame_status_view *view) {
  if (view == NULL) return;
  qa_app_cgame_status_cvars_destroy(view->wrapper);
  free(view->records);
  free(view);
}
qa_app_qvm_cvar_services qa_app_cgame_status_view_cvars(qa_app_cgame_status_view *view) {
  return qa_app_cgame_status_cvars_services(view->wrapper);
}
static int32_t read_i32(const uint8_t *bytes) {
  int32_t v;
  memcpy(&v, bytes, sizeof v);
  return v;
}
static void write_i32(uint8_t *bytes, int32_t v) { memcpy(bytes, &v, sizeof v); }
static void record_set(qa_app_cgame_status_view *v, int32_t pointer, qa_qvm_memory *memory, int32_t handle, bool present) {
  for (size_t i = 0; i < v->record_count; i++) {
    if (v->records[i].pointer == pointer) {
      if (!present) {
        memmove(&v->records[i], &v->records[i + 1], (v->record_count - i - 1) * sizeof *v->records);
        v->record_count--;
      } else { v->records[i].memory = memory; v->records[i].handle = handle; }
      return;
    }
  }
  if (!present) return;
  if (v->record_count == v->record_capacity) {
    size_t cap = v->record_capacity == 0 ? 8 : v->record_capacity * 2;
    status_record *next = realloc(v->records, cap * sizeof *next);
    if (next == NULL) qa_range_error("Out of memory");
    v->records = next; v->record_capacity = cap;
  }
  v->records[v->record_count++] = (status_record){pointer, memory, handle};
}
bool qa_app_cgame_status_view_syscall(qa_app_cgame_status_view *view, uint8_t *words,
                                      size_t words_length, qa_qvm_memory *memory, double *out_result) {
  qa_qvm_host_result result = 0;
  const qa_qvm_game_memory game = qa_qvm_memory_game_memory(memory);
  bool has = qa_qvm_cvar_table_syscall("cgame", words, words_length, &game, &view->cvars, &result);
  int32_t trap = read_i32(words);
  if (has && (trap == 3 || trap == 4)) {
    int32_t pointer = read_i32(words + 4);
    qa_qvm_memory_span span;
    if (qa_qvm_memory_pointer(memory, pointer, &span)) {
      qa_qvm_writable_view record;
      qa_qvm_memory_view(memory, pointer, 272, 0, &record);
      int32_t handle = qa_qvm_writable_view_get_int32(&record, 0, true);
      qa_cvar_snapshot snapshot;
      bool found = false;
      qa_app_qvm_cvar_services services = qa_app_cgame_status_view_cvars(view);
      if (services.read_vm(services.ctx, handle, &snapshot)) {
        found = name_is_status(snapshot.name);
        qa_cvar_read_free(&snapshot);
      }
      record_set(view, pointer, memory, handle, found);
    }
  }
  *out_result = result;
  return has;
}
void qa_app_cgame_status_view_refresh(qa_app_cgame_status_view *view) {
  write_i32(view->refresh_words, 4);
  size_t i = 0;
  while (i < view->record_count) {
    status_record record = view->records[i];
    qa_qvm_writable_view record_view;
    qa_qvm_memory_view(record.memory, record.pointer, 272, 0, &record_view);
    if (qa_qvm_writable_view_get_int32(&record_view, 0, true) != record.handle) {
      memmove(&view->records[i], &view->records[i + 1], (view->record_count - i - 1) * sizeof *view->records);
      view->record_count--;
      continue;
    }
    write_i32(view->refresh_words + 4, record.pointer);
    qa_qvm_host_result ignored = 0;
    const qa_qvm_game_memory game = qa_qvm_memory_game_memory(record.memory);
    (void)qa_qvm_cvar_table_syscall("cgame", view->refresh_words, sizeof view->refresh_words, &game,
                                    &view->cvars, &ignored);
    i++;
  }
}
