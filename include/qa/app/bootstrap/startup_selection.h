/* Port of src/app/bootstrap/startup-selection.ts: StartupSelectionModel and
 * createStartupSelection over the discovered catalog (content/catalog), the
 * map archives, the entity parsers and the launch resolver.
 * C accommodations:
 *  - Async members are synchronous. TS throws reach the caller through
 *    qa_raise ("Error" for plain Errors, qa_range_error for RangeError), so a
 *    caller that catches (the startup menu) uses qa_try.
 *  - Results of rows(), presets(), monsterRosterRows(), monsterSourceRow(),
 *    summary(), teamArena.choices() and the mod menu rows are borrowed from
 *    the model until the next call of the same member (each has its own
 *    storage); strings in them are copies, so model mutations in between
 *    do not invalidate them.
 *  - The model owns every catalog it has used (the discovered one, Q3
 *    restricted ones, refreshed ones) until destroy, because recipes and
 *    options handed out borrow catalog product ids.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_STARTUP_SELECTION_H
#define QA_APP_BOOTSTRAP_STARTUP_SELECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/base_arena_selection.h"
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/protocol.h"
#include "qa/input/weapon_bindings.h"
#include "qa/ui/mods/menu.h"
#include "qa/ui/settings/action_catalog.h"

/* TS StartupSelectionChoice. unavailable/group/edition NULL = absent/null. */
typedef struct qa_app_startup_selection_choice {
  const char *id; const char *label; const char *unavailable;
  const char *group; const char *edition; /* "classic"|"rerelease" */
} qa_app_startup_selection_choice;
/* TS StartupSelectionRow; id is the StartupSelectionField text. */
typedef struct qa_app_startup_selection_row {
  const char *id; const char *label; const char *value;
  const qa_app_startup_selection_choice *choices; size_t choice_count;
} qa_app_startup_selection_row;
/* TS MonsterRosterRow. classname NULL = TS null. */
typedef struct qa_app_monster_roster_row {
  const char *classname; const char *label; const char *value; const char *effective_label;
  const qa_app_startup_selection_choice *choices; size_t choice_count;
} qa_app_monster_roster_row;
/* TS StartupNativePreset. choice.edition is unused (the preset edition is
 * `edition`); product_id/rules/mode NULL = absent. */
typedef struct qa_app_startup_native_preset {
  qa_app_startup_selection_choice choice;
  qa_game_family family;
  const char *edition;
  const qa_app_startup_selection_choice *difficulties; size_t difficulty_count;
  const char *default_skill;
  const char *product_id; const char *rules; const char *mode;
} qa_app_startup_native_preset;
/* TS StartupCharacterPreview. Strings borrowed from the model. */
typedef struct qa_app_startup_character_preview {
  const char *product; qa_game_family family; const char *model; const char *skin;
  double top_color, bottom_color;
} qa_app_startup_character_preview;
/* characterPreview overrides; NULL = TS undefined. */
typedef struct qa_app_startup_preview_overrides {
  const char *character, *model, *skin, *top_color, *bottom_color;
} qa_app_startup_preview_overrides;
/* TS StartupHosting. kind "offline"|"native-server"|"unified-server". */
typedef struct qa_app_startup_hosting {
  const char *kind; double port;
  bool has_q1_protocol; qa_q1_protocol_identity q1_protocol;
} qa_app_startup_hosting;
/* TS StartupLaunch. options is owned (qa_app_options_destroy through
 * qa_app_startup_launch_free); recipe borrows `backing` and the model's
 * catalogs, so free the launch before destroying the model. */
typedef struct qa_app_startup_launch {
  qa_app_options options;
  qa_executable_recipe recipe;
  void *backing;
} qa_app_startup_launch;
void qa_app_startup_launch_free(qa_app_startup_launch *launch);

/* Deep copy of ApplicationOptions (every owned member duplicated; borrowed
 * void* members shared). Aborts on OOM. */
void qa_app_startup_options_copy(qa_app_options *out, const qa_app_options *options);

typedef struct qa_app_startup_selection_model qa_app_startup_selection_model;
/* TS new StartupSelectionModel(catalog, initial). Takes ownership of
 * *catalog (a heap catalog, destroyed and freed with the model); options
 * are copied. */
qa_app_startup_selection_model *qa_app_startup_selection_model_new(qa_content_catalog *catalog,
    const qa_app_options *initial);
/* TS createStartupSelection(options): discover installed content (mods on)
 * and prepareMaps. */
qa_app_startup_selection_model *qa_app_create_startup_selection(const qa_app_options *options);
void qa_app_startup_selection_model_destroy(qa_app_startup_selection_model *m);

qa_content_catalog *qa_app_startup_selection_catalog(qa_app_startup_selection_model *m);
/* baseArenas(): NULL = TS null. */
const qa_app_arena_selection *qa_app_startup_selection_base_arenas(qa_app_startup_selection_model *m);
void qa_app_startup_selection_refresh_base_arenas(qa_app_startup_selection_model *m);
/* teamArena service. */
const qa_ui_choice *qa_app_startup_selection_team_choices(qa_app_startup_selection_model *m, size_t *out_count);
void qa_app_startup_selection_team_read(qa_app_startup_selection_model *m, const char **out_player, const char **out_opponent);
void qa_app_startup_selection_team_write(qa_app_startup_selection_model *m, const char *side, const char *team);
/* selectServerProfile(path): NULL is TS null. */
void qa_app_startup_selection_select_server_profile(qa_app_startup_selection_model *m, const char *path);
qa_app_startup_hosting qa_app_startup_selection_hosting(qa_app_startup_selection_model *m);
void qa_app_startup_selection_set_hosting(qa_app_startup_selection_model *m, qa_app_startup_hosting value);
/* TS mods (ModMenuService); ctx is the model. */
const qa_ui_mod_menu_service *qa_app_startup_selection_mods(qa_app_startup_selection_model *m);
void qa_app_startup_selection_refresh_catalog(qa_app_startup_selection_model *m);
void qa_app_startup_selection_prepare_maps(qa_app_startup_selection_model *m);
const qa_app_startup_native_preset *qa_app_startup_selection_presets(qa_app_startup_selection_model *m, size_t *out_count);
/* resolvePreset(id, difficulty?, arenaMap?). arena_map NULL = undefined. */
void qa_app_startup_selection_resolve_preset(qa_app_startup_selection_model *m, const char *id,
    bool has_difficulty, double difficulty, const char *arena_map, qa_app_startup_launch *out);
/* overrides NULL = {}. */
qa_app_startup_character_preview qa_app_startup_selection_character_preview(qa_app_startup_selection_model *m,
    const qa_app_startup_preview_overrides *overrides);
void qa_app_startup_selection_prepare_monster_roster(qa_app_startup_selection_model *m);
/* prepareMapChoices: false is the TS null; otherwise the preparation ran. */
bool qa_app_startup_selection_prepare_map_choices(qa_app_startup_selection_model *m, double offset, double count);
qa_app_startup_selection_row qa_app_startup_selection_monster_source_row(qa_app_startup_selection_model *m);
void qa_app_startup_selection_select_monster_source(qa_app_startup_selection_model *m, const char *id);
const qa_app_monster_roster_row *qa_app_startup_selection_monster_roster_rows(qa_app_startup_selection_model *m, size_t *out_count);
/* classname NULL is TS null. */
void qa_app_startup_selection_select_monster(qa_app_startup_selection_model *m, const char *classname, const char *id);
const qa_app_startup_selection_row *qa_app_startup_selection_rows(qa_app_startup_selection_model *m, size_t *out_count);
void qa_app_startup_selection_select(qa_app_startup_selection_model *m, const char *field, const char *id);
void qa_app_startup_selection_set_display(qa_app_startup_selection_model *m, double width, double height, double gamma);
/* TS get options(): a fresh owned copy (qa_app_options_destroy). */
void qa_app_startup_selection_options(qa_app_startup_selection_model *m, qa_app_options *out);
const char *const *qa_app_startup_selection_summary(qa_app_startup_selection_model *m, size_t *out_count);
/* Owned items; free with qa_weapon_binding_items_free. */
qa_weapon_binding_item *qa_app_startup_selection_binding_items(qa_app_startup_selection_model *m, size_t *out_count);
/* Borrowed until the next call. */
const char *qa_app_startup_selection_binding_items_key(qa_app_startup_selection_model *m);
qa_ui_binding_capabilities qa_app_startup_selection_binding_capabilities(qa_app_startup_selection_model *m);
void qa_app_startup_selection_resolve(qa_app_startup_selection_model *m, qa_app_startup_launch *out);
#endif
