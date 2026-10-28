/* Port of src/app/bootstrap/startup-selection.ts. Catalog discovery,
 * preset/map/character resolution and launch preparation run through
 * the host (landed content catalog + sibling bootstrap slices);
 * selection state (fields, hosting, server profile, display, monsters)
 * is kept here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_SELECTION_H
#define QA_APP_BOOTSTRAP_STARTUP_SELECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
/* TS StartupSelectionField / row / choice exact strings stay in labels. */
typedef struct qa_app_startup_selection_choice { const char *id; const char *label; } qa_app_startup_selection_choice;
typedef struct qa_app_startup_selection_row {
  const char *field; const char *label; const char *value;
  const qa_app_startup_selection_choice *choices; size_t choice_count;
} qa_app_startup_selection_row;
typedef struct qa_app_startup_hosting { bool dedicated; double listen_capacity; } qa_app_startup_hosting;
typedef struct qa_app_startup_launch { const void *payload; } qa_app_startup_launch;
typedef struct qa_app_startup_native_preset { const char *id; const char *label; } qa_app_startup_native_preset;
typedef struct qa_app_startup_character_preview { const void *payload; } qa_app_startup_character_preview;
typedef struct qa_app_monster_roster_row { const char *classname; const char *selected; } qa_app_monster_roster_row;
typedef struct qa_app_startup_selection_host {
  void *ctx;
  /* Hosted over the current catalog (host swaps its own catalog). */
  const char *(*refresh_catalog)(void *ctx);
  const char *(*arena_selection)(void *ctx, const void **out);
  const char *(*refresh_base_arenas)(void *ctx);
  const char *(*prepare_maps)(void *ctx);
  const qa_app_startup_native_preset *(*presets)(void *ctx, size_t *out_count);
  const char *(*resolve_preset)(void *ctx, const char *id, bool has_difficulty, double difficulty, const char *arena_map, qa_app_startup_launch *out);
  qa_app_startup_character_preview (*character_preview)(void *ctx, const qa_app_options *options,
      const char *character, const char *model, const char *skin, const char *top_color, const char *bottom_color);
  const char *(*prepare_monster_roster)(void *ctx);
  const char *(*prepare_map_choices)(void *ctx, double offset, double count);
  qa_app_startup_selection_row (*monster_source_row)(void *ctx);
  const qa_app_monster_roster_row *(*monster_roster_rows)(void *ctx, size_t *out_count);
  const qa_app_startup_selection_row *(*rows)(void *ctx, size_t *out_count);
  const char *(*binding_items)(void *ctx, const void ***out_items, size_t *out_count);
  const void *(*binding_capabilities)(void *ctx);
  const char *(*resolve)(void *ctx, const qa_app_options *options, qa_app_startup_launch *out);
  /* Field value getters for summary/options (mirror of private fields). */
  const char *const *(*summary)(void *ctx, const qa_app_options *options, size_t *out_count);
} qa_app_startup_selection_host;
typedef struct qa_app_startup_selection_model qa_app_startup_selection_model;
qa_app_startup_selection_model *qa_app_startup_selection_model_create(qa_app_startup_selection_host host, qa_app_options initial);
void qa_app_startup_selection_model_destroy(qa_app_startup_selection_model *m);
bool qa_app_startup_selection_base_arenas(qa_app_startup_selection_model *m, const void **out);
void qa_app_startup_selection_refresh_base_arenas(qa_app_startup_selection_model *m);
void qa_app_startup_selection_select_server_profile(qa_app_startup_selection_model *m, const char *path);
qa_app_startup_hosting qa_app_startup_selection_hosting(qa_app_startup_selection_model *m);
void qa_app_startup_selection_set_hosting(qa_app_startup_selection_model *m, qa_app_startup_hosting value);
void qa_app_startup_selection_refresh_catalog(qa_app_startup_selection_model *m);
void qa_app_startup_selection_prepare_maps(qa_app_startup_selection_model *m);
const qa_app_startup_native_preset *qa_app_startup_selection_presets(qa_app_startup_selection_model *m, size_t *out_count);
qa_app_startup_launch qa_app_startup_selection_resolve_preset(qa_app_startup_selection_model *m, const char *id,
                                                              bool has_difficulty, double difficulty, const char *arena_map);
qa_app_startup_character_preview qa_app_startup_selection_character_preview(qa_app_startup_selection_model *m,
    const char *character, const char *model, const char *skin, const char *top_color, const char *bottom_color);
void qa_app_startup_selection_prepare_monster_roster(qa_app_startup_selection_model *m);
bool qa_app_startup_selection_prepare_map_choices(qa_app_startup_selection_model *m, double offset, double count);
qa_app_startup_selection_row qa_app_startup_selection_monster_source_row(qa_app_startup_selection_model *m);
void qa_app_startup_selection_select_monster_source(qa_app_startup_selection_model *m, const char *id);
const qa_app_monster_roster_row *qa_app_startup_selection_monster_roster_rows(qa_app_startup_selection_model *m, size_t *out_count);
void qa_app_startup_selection_select_monster(qa_app_startup_selection_model *m, const char *classname, const char *id);
const qa_app_startup_selection_row *qa_app_startup_selection_rows(qa_app_startup_selection_model *m, size_t *out_count);
void qa_app_startup_selection_select(qa_app_startup_selection_model *m, const char *field, const char *id);
void qa_app_startup_selection_set_display(qa_app_startup_selection_model *m, double width, double height, double gamma);
const qa_app_options *qa_app_startup_selection_options(qa_app_startup_selection_model *m);
const char *const *qa_app_startup_selection_summary(qa_app_startup_selection_model *m, size_t *out_count);
const void **qa_app_startup_selection_binding_items(qa_app_startup_selection_model *m, size_t *out_count);
const void *qa_app_startup_selection_binding_capabilities(qa_app_startup_selection_model *m);
qa_app_startup_launch qa_app_startup_selection_resolve(qa_app_startup_selection_model *m);
#endif
