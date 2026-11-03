/* Port of src/content/q3/base/game/save-module-values.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/save_module_values.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
static char *dupstr(const char *v) { if (!v) return NULL; size_t n = strlen(v); char *d = malloc(n + 1); if (d) memcpy(d, v, n + 1); return d; }
qa_q3_game_entity *qa_q3_read_module_entity(const qa_save_reader *reader, qa_q3_entity_pool *pool) {
  double slot = qa_save_reader_integer_min(reader, 0);
  if (slot > 1023) qa_save_reader_fail(reader, "module source entity slot exceeds table extent");
  qa_q3_game_entity *e = qa_q3_entity_pool_get(pool, slot);
  if (!e) qa_save_reader_fail(reader, "module references an absent source entity slot");
  return e;
}
qa_q3_module_cvar_capture qa_q3_capture_module_cvar(const qa_cvar_snapshot *v) {
  qa_q3_module_cvar_capture c = {v->name, v->value, v->reset_value, v->latched_value, v->flags, v->modified, v->modification_count, v->numeric_value, v->integer_value}; return c;
}
static const char *rf_str(const qa_save_reader *r, const char *n) { qa_save_reader f; qa_save_reader_field(r, n, &f); const char *v = qa_save_reader_string(&f); qa_save_reader_destroy(&f); return v; }
qa_cvar_snapshot qa_q3_read_module_cvar(const qa_save_reader *reader) {
  qa_cvar_snapshot v; qa_save_reader f;
  v.name = dupstr(rf_str(reader, "name")); v.value = dupstr(rf_str(reader, "value")); v.reset_value = dupstr(rf_str(reader, "resetValue"));
  qa_save_reader_field(reader, "latchedValue", &f); v.latched_value = qa_save_reader_is_null(&f) ? NULL : dupstr(qa_save_reader_string(&f)); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "flags", &f); v.flags = qa_js_to_int32(qa_save_reader_integer(&f)); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "modified", &f); v.modified = qa_save_reader_boolean(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "modificationCount", &f); v.modification_count = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "numericValue", &f); v.numeric_value = qa_save_reader_number(&f); qa_save_reader_destroy(&f);
  qa_save_reader_field(reader, "integerValue", &f); v.integer_value = qa_save_reader_integer(&f); qa_save_reader_destroy(&f);
  return v;
}
