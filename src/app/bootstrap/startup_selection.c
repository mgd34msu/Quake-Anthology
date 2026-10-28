/* Port of src/app/bootstrap/startup-selection.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/startup_selection.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/input/support.h"
struct qa_app_startup_selection_model {
  qa_app_startup_selection_host host;
  qa_app_options options;
  bool has_server_profile; char *server_profile_path;
  qa_app_startup_hosting hosting;
  /* Selected field values (TS private selection record). */
  struct { char *field; char *value; } *fields; size_t field_count, field_cap;
  char *monster_source;
  struct { char *classname; char *id; } *monsters; size_t monster_count, monster_cap;
};
static char *xdup(const char *s) { char *r = qa_input_dup(s); if (!r) qa_range_error("Out of memory"); return r; }
static const char *field_value(qa_app_startup_selection_model *m, const char *field) {
  for (size_t i = 0; i < m->field_count; i++) if (strcmp(m->fields[i].field, field) == 0) return m->fields[i].value;
  return NULL;
}
qa_app_startup_selection_model *qa_app_startup_selection_model_create(qa_app_startup_selection_host host, qa_app_options initial) {
  qa_app_startup_selection_model *m = calloc(1, sizeof *m);
  if (!m) return NULL;
  m->host = host;
  m->options = initial;
  m->hosting = (qa_app_startup_hosting){initial.dedicated, initial.seats};
  return m;
}
void qa_app_startup_selection_model_destroy(qa_app_startup_selection_model *m) {
  if (!m) return;
  qa_app_options_destroy(&m->options);
  free(m->server_profile_path); free(m->monster_source);
  for (size_t i = 0; i < m->field_count; i++) { free(m->fields[i].field); free(m->fields[i].value); }
  free(m->fields);
  for (size_t i = 0; i < m->monster_count; i++) { free(m->monsters[i].classname); free(m->monsters[i].id); }
  free(m->monsters);
  free(m);
}
static void checked(const char *error) { if (error != NULL) qa_range_error("%s", error); }
bool qa_app_startup_selection_base_arenas(qa_app_startup_selection_model *m, const void **out) {
  return m->host.arena_selection(m->host.ctx, out) == NULL && *out != NULL;
}
void qa_app_startup_selection_refresh_base_arenas(qa_app_startup_selection_model *m) { checked(m->host.refresh_base_arenas(m->host.ctx)); }
void qa_app_startup_selection_select_server_profile(qa_app_startup_selection_model *m, const char *path) {
  free(m->server_profile_path);
  m->server_profile_path = path ? xdup(path) : NULL;
  m->has_server_profile = path != NULL;
}
qa_app_startup_hosting qa_app_startup_selection_hosting(qa_app_startup_selection_model *m) { return m->hosting; }
void qa_app_startup_selection_set_hosting(qa_app_startup_selection_model *m, qa_app_startup_hosting value) {
  m->hosting = value;
  m->options.dedicated = value.dedicated;
  m->options.seats = value.listen_capacity;
}
void qa_app_startup_selection_refresh_catalog(qa_app_startup_selection_model *m) { checked(m->host.refresh_catalog(m->host.ctx)); }
void qa_app_startup_selection_prepare_maps(qa_app_startup_selection_model *m) { checked(m->host.prepare_maps(m->host.ctx)); }
const qa_app_startup_native_preset *qa_app_startup_selection_presets(qa_app_startup_selection_model *m, size_t *out_count) {
  return m->host.presets(m->host.ctx, out_count);
}
qa_app_startup_launch qa_app_startup_selection_resolve_preset(qa_app_startup_selection_model *m, const char *id,
                                                              bool has_difficulty, double difficulty, const char *arena_map) {
  qa_app_startup_launch launch; memset(&launch, 0, sizeof launch);
  checked(m->host.resolve_preset(m->host.ctx, id, has_difficulty, difficulty, arena_map, &launch));
  return launch;
}
qa_app_startup_character_preview qa_app_startup_selection_character_preview(qa_app_startup_selection_model *m,
    const char *character, const char *model, const char *skin, const char *top_color, const char *bottom_color) {
  return m->host.character_preview(m->host.ctx, &m->options,
      character ? character : field_value(m, "character"),
      model ? model : field_value(m, "model"),
      skin ? skin : field_value(m, "skin"),
      top_color ? top_color : field_value(m, "topColor"),
      bottom_color ? bottom_color : field_value(m, "bottomColor"));
}
void qa_app_startup_selection_prepare_monster_roster(qa_app_startup_selection_model *m) { checked(m->host.prepare_monster_roster(m->host.ctx)); }
bool qa_app_startup_selection_prepare_map_choices(qa_app_startup_selection_model *m, double offset, double count) {
  const char *error = m->host.prepare_map_choices(m->host.ctx, offset, count);
  if (error != NULL && strcmp(error, "") == 0) return false; /* TS null sentinel */
  checked(error);
  return true;
}
qa_app_startup_selection_row qa_app_startup_selection_monster_source_row(qa_app_startup_selection_model *m) {
  return m->host.monster_source_row(m->host.ctx);
}
void qa_app_startup_selection_select_monster_source(qa_app_startup_selection_model *m, const char *id) {
  free(m->monster_source); m->monster_source = xdup(id);
}
const qa_app_monster_roster_row *qa_app_startup_selection_monster_roster_rows(qa_app_startup_selection_model *m, size_t *out_count) {
  return m->host.monster_roster_rows(m->host.ctx, out_count);
}
void qa_app_startup_selection_select_monster(qa_app_startup_selection_model *m, const char *classname, const char *id) {
  if (classname == NULL) {
    for (size_t i = 0; i < m->monster_count; i++) { free(m->monsters[i].id); m->monsters[i].id = xdup(id); }
    return;
  }
  for (size_t i = 0; i < m->monster_count; i++) if (strcmp(m->monsters[i].classname, classname) == 0) { free(m->monsters[i].id); m->monsters[i].id = xdup(id); return; }
  if (m->monster_count == m->monster_cap) { m->monster_cap = m->monster_cap ? m->monster_cap * 2 : 8; m->monsters = realloc(m->monsters, m->monster_cap * sizeof *m->monsters); if (!m->monsters) qa_range_error("Out of memory"); }
  m->monsters[m->monster_count].classname = xdup(classname);
  m->monsters[m->monster_count].id = xdup(id);
  m->monster_count++;
}
const qa_app_startup_selection_row *qa_app_startup_selection_rows(qa_app_startup_selection_model *m, size_t *out_count) {
  return m->host.rows(m->host.ctx, out_count);
}
void qa_app_startup_selection_select(qa_app_startup_selection_model *m, const char *field, const char *id) {
  for (size_t i = 0; i < m->field_count; i++) if (strcmp(m->fields[i].field, field) == 0) { free(m->fields[i].value); m->fields[i].value = xdup(id); return; }
  if (m->field_count == m->field_cap) { m->field_cap = m->field_cap ? m->field_cap * 2 : 16; m->fields = realloc(m->fields, m->field_cap * sizeof *m->fields); if (!m->fields) qa_range_error("Out of memory"); }
  m->fields[m->field_count].field = xdup(field);
  m->fields[m->field_count].value = xdup(id);
  m->field_count++;
}
void qa_app_startup_selection_set_display(qa_app_startup_selection_model *m, double width, double height, double gamma) {
  m->options.width = width;
  m->options.height = height;
  m->options.gamma = gamma;
}
const qa_app_options *qa_app_startup_selection_options(qa_app_startup_selection_model *m) { return &m->options; }
const char *const *qa_app_startup_selection_summary(qa_app_startup_selection_model *m, size_t *out_count) {
  return m->host.summary(m->host.ctx, &m->options, out_count);
}
const void **qa_app_startup_selection_binding_items(qa_app_startup_selection_model *m, size_t *out_count) {
  const void **items = NULL;
  checked(m->host.binding_items(m->host.ctx, &items, out_count));
  return items;
}
const void *qa_app_startup_selection_binding_capabilities(qa_app_startup_selection_model *m) {
  return m->host.binding_capabilities(m->host.ctx);
}
qa_app_startup_launch qa_app_startup_selection_resolve(qa_app_startup_selection_model *m) {
  qa_app_startup_launch launch; memset(&launch, 0, sizeof launch);
  checked(m->host.resolve(m->host.ctx, &m->options, &launch));
  return launch;
}
