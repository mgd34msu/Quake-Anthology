/* Port of src/content/q3/presentation/mission-hud.ts. GPL-2.0-or-later. */
#include "qa/content/q3/presentation/mission_hud.h"
#include "qa/network/common/js_number.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/shared/items.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/content/q3/presentation/draw_tools.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/ui/common/legacy/script/memory.h"
#include "qa/input/key_codes.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"
typedef struct team_order { const char *personal, *team, *button; } team_order;
static const team_order ORDERS[] = {
  {"onoffense", "offense", "+button7; wait; -button7"},
  {"ondefense", "defend", "+button8; wait; -button8"},
  {"onpatrol", "patrol", "+button9; wait; -button9"},
  {"onfollow", "followme", "+button10; wait; -button10"},
  {"ongetflag", "returnflag", NULL},
  {"onfollowcarrier", "followflagcarrier", NULL},
  {"oncamping", "camp", NULL},
};
enum { ORDER_COUNT = 7, MENU_ROOT_LIMIT = 4096, MENU_BUFFER_LIMIT = 32768 };
typedef struct map_entry {
  char *key;   /* folded path; NULL is the TS null key */
  bool defined; /* false is the stored TS undefined */
  qa_picture_asset picture;
  qa_pcm_sound *sound;
  qa_q3_pres_scene_model model;
} map_entry;
struct qa_q3_pres_mission_hud_map { map_entry *entries; size_t count, capacity; };
typedef struct font_entry { char *path; double point_size; const qa_q3_registered_font *font; } font_entry;
struct qa_q3_pres_mission_hud_font_map { font_entry *entries; size_t count, capacity; };
static void oom(void) { qa_range_error("Mission HUD allocation failed"); }
static void check_at(double index, double length) {
  if (index != trunc(index) || index < 0 || index >= length)
    qa_range_error("Mission HUD source index %s outside %s", QA_JS_NUMBER(index), QA_JS_NUMBER(length));
}
static void opened(const qa_q3_pres_mission_hud *hud) {
  if (hud->closed) qa_range_error("Mission HUD is disposed");
}
static void registration_current(const qa_q3_pres_mission_hud *hud, double generation) {
  opened(hud);
  if (generation != hud->generation) qa_range_error("Mission HUD media registration belongs to a retired lifecycle");
}
static qa_q3_base_source_player_state *snapshot_of(const qa_q3_pres_mission_hud *hud) {
  if (!hud->state->snap) qa_range_error("Mission HUD operation requires cg.snap");
  return &hud->state->snap->player_state;
}
static void source_text(const char *value, size_t maximum) {
  if (strlen(value) >= maximum) qa_range_error("Mission HUD source string exceeds %zu bytes", maximum - 1);
}
static char *folded_copy(const char *text) {
  size_t length = strlen(text);
  char *copy = malloc(length + 1);
  if (!copy) oom();
  for (size_t i = 0; i <= length; i++) copy[i] = text[i] >= 'A' && text[i] <= 'Z' ? (char)(text[i] + 32) : text[i];
  return copy;
}
static map_entry *map_slot(qa_q3_pres_mission_hud_map *map, const char *path, bool create) {
  char *key = path ? folded_copy(path) : NULL;
  for (size_t i = 0; i < map->count; i++) {
    map_entry *entry = &map->entries[i];
    if ((!key && !entry->key) || (key && entry->key && strcmp(key, entry->key) == 0)) {
      free(key);
      return entry;
    }
  }
  if (!create) {
    free(key);
    return NULL;
  }
  if (map->count == map->capacity) {
    size_t capacity = map->capacity ? map->capacity * 2 : 32;
    map_entry *entries = realloc(map->entries, capacity * sizeof *entries);
    if (!entries) oom();
    map->entries = entries;
    map->capacity = capacity;
  }
  map_entry *entry = &map->entries[map->count++];
  memset(entry, 0, sizeof *entry);
  entry->key = key;
  return entry;
}
static void map_destroy(qa_q3_pres_mission_hud_map *map) {
  if (!map) return;
  for (size_t i = 0; i < map->count; i++) free(map->entries[i].key);
  free(map->entries);
  free(map);
}
static qa_picture_asset material_asset(qa_material_picture material) {
  qa_picture_asset picture;
  memset(&picture, 0, sizeof picture);
  picture.kind = QA_PICTURE_MATERIAL;
  picture.material = material;
  return picture;
}
static qa_picture_asset picture_of(const qa_q3_pres_mission_hud *hud, const qa_q3_pres_scene_shader *shader) {
  return material_asset(hud->media->resources.picture(hud->media->resources.ctx, shader));
}
static void print_text(qa_q3_pres_mission_hud *hud, const char *text) { hud->host.print(hud->host.ctx, text); }
static void memory_print(void *ctx, const char *text) { print_text(ctx, text); }
static double vm_integer(qa_q3_pres_mission_hud *hud, const char *name) {
  qa_cvar_snapshot snapshot;
  if (!qa_q3_pres_client_configuration_read_vm_cvar(hud->host.configuration, name, &snapshot)) oom();
  double value = snapshot.integer_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static double vm_numeric(qa_q3_pres_mission_hud *hud, const char *name) {
  qa_cvar_snapshot snapshot;
  if (!qa_q3_pres_client_configuration_read_vm_cvar(hud->host.configuration, name, &snapshot)) oom();
  double value = snapshot.numeric_value;
  qa_cvar_read_free(&snapshot);
  return value;
}
static double selected_index(qa_q3_pres_mission_hud *hud) { return vm_integer(hud, "cg_currentSelectedPlayer"); }
static void set_selected(qa_q3_pres_mission_hud *hud, double value) {
  if (!qa_q3_pres_client_configuration_set_vm_integer(hud->host.configuration, QA_Q3_PRES_VM_CG_CURRENT_SELECTED_PLAYER, value)) oom();
}
static void set_cvar(qa_q3_pres_mission_hud *hud, const char *name, const char *value) {
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_set(hud->host.cvars, name, value, true, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void append_command(qa_q3_pres_mission_hud *hud, const char *text) { hud->host.append(hud->host.ctx, text); }
static void zero_font(qa_q3_registered_font *font) {
  memset(font, 0, sizeof *font);
  font->glyph_count = QA_Q3_FONT_GLYPHS;
}
/* ---- owner-draw host ---- */
static const qa_q3_font_set *owner_fonts(void *ctx) { return qa_q3_pres_mission_hud_fonts(ctx); }
static qa_cvar_snapshot owner_cvar(void *ctx, const char *name) {
  qa_q3_pres_mission_hud *hud = ctx;
  qa_cvar_snapshot snapshot;
  if (!qa_q3_pres_client_configuration_read_vm_cvar(hud->host.configuration, name, &snapshot)) oom();
  return snapshot;
}
static const char *owner_config_string(void *ctx, double index) {
  qa_q3_pres_mission_hud *hud = ctx;
  return hud->host.config_string(hud->host.ctx, index);
}
static double owner_selected(void *ctx) { return qa_q3_pres_mission_hud_get_selected_player(ctx); }
static void owner_chat(void *ctx, const char **system, const char **team1, const char **team2) {
  qa_q3_pres_mission_hud *hud = ctx;
  *system = hud->system_chat;
  *team1 = hud->team_chat1;
  *team2 = hud->team_chat2;
}
static const qa_q3_pres_scene_shader *owner_weapon_icon(void *ctx, double weapon) {
  qa_q3_pres_mission_hud *hud = ctx;
  return qa_q3_pres_client_weapon_media_registry_weapon(hud->host.weapon_registry, weapon)->weapon_icon;
}
static const qa_q3_pres_scene_shader *owner_ammo_icon(void *ctx, double weapon) {
  qa_q3_pres_mission_hud *hud = ctx;
  return qa_q3_pres_client_weapon_media_registry_weapon(hud->host.weapon_registry, weapon)->ammo_icon;
}
static qa_q3_pres_scene_model owner_ammo_model(void *ctx, double weapon) {
  qa_q3_pres_mission_hud *hud = ctx;
  return qa_q3_pres_client_weapon_media_registry_weapon(hud->host.weapon_registry, weapon)->ammo_model;
}
static bool owner_register_item_visuals(void *ctx, double index) {
  return qa_q3_pres_client_weapon_media_registry_register_item_visuals(((qa_q3_pres_mission_hud *)ctx)->host.weapon_registry, index);
}
static const qa_q3_pres_scene_shader *owner_item_icon(void *ctx, double index) {
  qa_q3_pres_mission_hud *hud = ctx;
  size_t count = 0;
  const qa_q3_pres_packet_item_visual *items = qa_q3_pres_client_weapon_media_registry_items(hud->host.weapon_registry, &count);
  check_at(index, (double)count);
  return items[(size_t)index].icon;
}
static const qa_q3_pres_scene_shader *owner_register_shader(void *ctx, const char *path) {
  qa_q3_pres_mission_hud *hud = ctx;
  const qa_q3_pres_scene_shader *shader = NULL;
  if (!hud->media->resources.register_shader(hud->media->resources.ctx, path, &shader)) oom();
  return shader;
}
static double owner_crandom(void *ctx) { return qa_game_random_crandom(((qa_q3_pres_mission_hud *)ctx)->host.random); }
static double owner_random(void *ctx) { return qa_game_random_random(((qa_q3_pres_mission_hud *)ctx)->host.random); }
bool qa_q3_pres_mission_hud_init(qa_q3_pres_mission_hud *hud, qa_q3_pres_client_game_state *state,
    qa_q3_pres_client_game_static_state *static_state, qa_q3_pres_client_media *media,
    qa_q3_pres_mission_hud_host host) {
  if (state->product != QA_Q3_BASE_PRODUCT_MISSIONPACK || static_state->product != state->product ||
      media->static_state != static_state || host.icons->state != state || host.icons->media.static_state != static_state)
    qa_range_error("Mission HUD requires one Team Arena cgame state and media owner");
  memset(hud, 0, sizeof *hud);
  hud->state = state;
  hud->static_state = static_state;
  hud->media = media;
  hud->host = host;
  hud->scoreboard_first_time = true;
  zero_font(&hud->small_font);
  zero_font(&hud->normal_font);
  zero_font(&hud->big_font);
  hud->fonts.small = &hud->small_font;
  hud->fonts.normal = &hud->normal_font;
  hud->fonts.big = &hud->big_font;
  hud->fonts.profile = QA_Q3_FONT_CGAME;
  hud->pictures = calloc(1, sizeof *hud->pictures);
  hud->sounds = calloc(1, sizeof *hud->sounds);
  hud->models = calloc(1, sizeof *hud->models);
  hud->registered_fonts = calloc(1, sizeof *hud->registered_fonts);
  hud->memory = qa_ui_team_arena_memory_create("qvm32", memory_print, hud, QA_UI_TEAM_ARENA_MODULE_CGAME);
  if (!hud->pictures || !hud->sounds || !hud->models || !hud->registered_fonts || !hud->memory) {
    qa_q3_pres_mission_hud_destroy(hud);
    return false;
  }
  qa_q3_pres_mission_owner_draw_host owner;
  memset(&owner, 0, sizeof owner);
  owner.icons = host.icons;
  owner.ctx = hud;
  owner.fonts = owner_fonts;
  owner.read_vm_cvar = owner_cvar;
  owner.config_string = owner_config_string;
  owner.selected_player = owner_selected;
  owner.chat = owner_chat;
  owner.weapon_icon = owner_weapon_icon;
  owner.register_item_visuals = owner_register_item_visuals;
  owner.item_icon = owner_item_icon;
  owner.graphics = &media->graphics;
  owner.register_shader = owner_register_shader;
  owner.crandom = owner_crandom;
  owner.random_value = owner_random;
  owner.weapon_hud_ctx = host.weapon_hud_ctx;
  owner.weapon_hud = host.weapon_hud;
  owner.ammo_icon = owner_ammo_icon;
  owner.ammo_model = owner_ammo_model;
  qa_q3_pres_mission_owner_draw_init(&hud->owner_draw, state, static_state, owner);
  qa_picture_asset zero = picture_of(hud, NULL);
  hud->widget_assets.white_shader = picture_of(hud, media->graphics.white_shader);
  hud->widget_assets.gradient_bar = zero;
  hud->widget_assets.scroll_bar = zero;
  hud->widget_assets.scroll_bar_arrow_down = zero;
  hud->widget_assets.scroll_bar_arrow_up = zero;
  hud->widget_assets.scroll_bar_arrow_left = zero;
  hud->widget_assets.scroll_bar_arrow_right = zero;
  hud->widget_assets.scroll_bar_thumb = zero;
  hud->widget_assets.slider_bar = zero;
  hud->widget_assets.slider_thumb = zero;
  return true;
}
void qa_q3_pres_mission_hud_dispose(qa_q3_pres_mission_hud *hud) {
  if (hud->closed) return;
  hud->generation++;
  hud->closed = true;
  if (hud->runtime) {
    qa_ui_runtime_retire(hud->runtime);
    qa_ui_runtime_destroy(hud->runtime);
    hud->runtime = NULL;
  }
  hud->captured_menu = NULL;
}
void qa_q3_pres_mission_hud_destroy(qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_mission_hud_dispose(hud);
  /* Captured menus may read any generation until here, as under the TS GC. */
  for (size_t i = 0; i < hud->definition_count; i++) {
    qa_ui_menu_definitions_destroy(hud->definitions[i]);
    free(hud->definitions[i]);
  }
  free(hud->definitions);
  hud->asset_definitions = NULL;
  hud->definitions = NULL;
  hud->definition_count = 0;
  map_destroy(hud->pictures);
  map_destroy(hud->sounds);
  map_destroy(hud->models);
  hud->pictures = hud->sounds = hud->models = NULL;
  if (hud->registered_fonts) {
    for (size_t i = 0; i < hud->registered_fonts->count; i++) free(hud->registered_fonts->entries[i].path);
    free(hud->registered_fonts->entries);
    free(hud->registered_fonts);
    hud->registered_fonts = NULL;
  }
  if (hud->memory) qa_ui_team_arena_memory_destroy(hud->memory);
  hud->memory = NULL;
}
const qa_q3_font_set *qa_q3_pres_mission_hud_fonts(qa_q3_pres_mission_hud *hud) {
  hud->fonts.small_threshold = vm_numeric(hud, "ui_smallFont");
  hud->fonts.big_threshold = vm_numeric(hud, "ui_bigFont");
  return &hud->fonts;
}
qa_q3_pres_mission_hud_cached_assets qa_q3_pres_mission_hud_cached(const qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_mission_hud_cached_assets assets;
  assets.fx_base = hud->fx_base;
  memcpy(assets.fx_colors, hud->fx_colors, sizeof assets.fx_colors);
  assets.widgets = hud->widget_assets;
  return assets;
}
bool qa_q3_pres_mission_hud_menu_state(qa_q3_pres_mission_hud *hud, qa_ui_runtime_snapshot *out) {
  if (!hud->runtime) return false;
  *out = qa_ui_runtime_take_snapshot(hud->runtime);
  return true;
}
static bool cache_shader(qa_q3_pres_mission_hud *hud, double generation, const char *path, const qa_q3_pres_scene_shader **out) {
  if (!hud->media->resources.register_shader_no_mip(hud->media->resources.ctx, path, out)) return false;
  opened(hud);
  if (generation != hud->generation) qa_range_error("Mission HUD asset registration belongs to a retired lifecycle");
  return true;
}
static bool cache_picture(qa_q3_pres_mission_hud *hud, double generation, const char *path, qa_picture_asset *out) {
  const qa_q3_pres_scene_shader *shader = NULL;
  if (!cache_shader(hud, generation, path, &shader)) return false;
  *out = picture_of(hud, shader);
  return true;
}
bool qa_q3_pres_mission_hud_asset_cache(qa_q3_pres_mission_hud *hud) {
  opened(hud);
  double generation = hud->generation;
  qa_ui_widget_assets widgets = hud->widget_assets;
  if (!cache_picture(hud, generation, "ui/assets/gradientbar2.tga", &widgets.gradient_bar)) return false;
  if (!cache_shader(hud, generation, "menu/art/fx_base", &hud->fx_base)) return false;
  static const char *const colors[7] = {"red", "yel", "grn", "teal", "blue", "cyan", "white"};
  for (int i = 0; i < 7; i++) {
    char path[32];
    snprintf(path, sizeof path, "menu/art/fx_%s", colors[i]);
    if (!cache_shader(hud, generation, path, &hud->fx_colors[i])) return false;
  }
  if (!cache_picture(hud, generation, "ui/assets/scrollbar.tga", &widgets.scroll_bar)) return false;
  if (!cache_picture(hud, generation, "ui/assets/scrollbar_arrow_dwn_a.tga", &widgets.scroll_bar_arrow_down)) return false;
  if (!cache_picture(hud, generation, "ui/assets/scrollbar_arrow_up_a.tga", &widgets.scroll_bar_arrow_up)) return false;
  if (!cache_picture(hud, generation, "ui/assets/scrollbar_arrow_left.tga", &widgets.scroll_bar_arrow_left)) return false;
  if (!cache_picture(hud, generation, "ui/assets/scrollbar_arrow_right.tga", &widgets.scroll_bar_arrow_right)) return false;
  if (!cache_picture(hud, generation, "ui/assets/scrollbar_thumb.tga", &widgets.scroll_bar_thumb)) return false;
  if (!cache_picture(hud, generation, "ui/assets/slider2.tga", &widgets.slider_bar)) return false;
  if (!cache_picture(hud, generation, "ui/assets/sliderbutt_1.tga", &widgets.slider_thumb)) return false;
  opened(hud);
  widgets.white_shader = picture_of(hud, hud->media->graphics.white_shader);
  hud->widget_assets = widgets;
  return true;
}
bool qa_q3_pres_mission_hud_other_team_has_flag(qa_q3_pres_mission_hud *hud) {
  return qa_q3_pres_mission_owner_draw_other_team_has_flag(&hud->owner_draw);
}
bool qa_q3_pres_mission_hud_your_team_has_flag(qa_q3_pres_mission_hud *hud) {
  return qa_q3_pres_mission_owner_draw_your_team_has_flag(&hud->owner_draw);
}
/* ---- registration caches ---- */
static map_entry *register_picture(qa_q3_pres_mission_hud *hud, const char *path) {
  opened(hud);
  double generation = hud->generation;
  const qa_q3_pres_scene_shader *shader = NULL;
  if (!hud->media->resources.register_shader_no_mip(hud->media->resources.ctx, path, &shader)) oom();
  registration_current(hud, generation);
  map_entry *entry = map_slot(hud->pictures, path, true);
  entry->defined = shader != NULL;
  if (shader) entry->picture = picture_of(hud, shader);
  return entry;
}
static map_entry *register_sound(qa_q3_pres_mission_hud *hud, const char *path) {
  opened(hud);
  double generation = hud->generation;
  qa_pcm_sound *sound = NULL;
  if (!hud->media->sound_bank.register_sound(hud->media->sound_bank.ctx, path, false, &sound)) oom();
  registration_current(hud, generation);
  map_entry *entry = map_slot(hud->sounds, path, true);
  entry->defined = sound != NULL;
  entry->sound = sound;
  return entry;
}
static map_entry *register_model(qa_q3_pres_mission_hud *hud, const char *path) {
  opened(hud);
  double generation = hud->generation;
  qa_q3_pres_scene_model model;
  if (!hud->media->resources.register_model(hud->media->resources.ctx, path, &model)) oom();
  registration_current(hud, generation);
  map_entry *entry = map_slot(hud->models, path, true);
  entry->defined = true;
  entry->model = model;
  return entry;
}
static font_entry *font_slot(qa_q3_pres_mission_hud *hud, const char *path, double point_size) {
  qa_q3_pres_mission_hud_font_map *map = hud->registered_fonts;
  for (size_t i = 0; i < map->count; i++) {
    font_entry *entry = &map->entries[i];
    bool same = (!path && !entry->path) || (path && entry->path && strcmp(path, entry->path) == 0);
    if (same && entry->point_size == point_size) return entry;
  }
  return NULL;
}
static void register_font(qa_q3_pres_mission_hud *hud, const char *path, double point_size) {
  opened(hud);
  double generation = hud->generation;
  const qa_q3_registered_font *font = qa_q3_ui_asset_registry_register_font(hud->host.font_registry, path, point_size);
  registration_current(hud, generation);
  font_entry *entry = font_slot(hud, path, point_size);
  if (!entry) {
    qa_q3_pres_mission_hud_font_map *map = hud->registered_fonts;
    if (map->count == map->capacity) {
      size_t capacity = map->capacity ? map->capacity * 2 : 16;
      font_entry *entries = realloc(map->entries, capacity * sizeof *entries);
      if (!entries) oom();
      map->entries = entries;
      map->capacity = capacity;
    }
    entry = &map->entries[map->count++];
    entry->path = NULL;
    if (path) {
      entry->path = strdup(path);
      if (!entry->path) oom();
    }
    entry->point_size = point_size;
  }
  entry->font = font;
}
static const qa_q3_registered_font *font_for(qa_q3_pres_mission_hud *hud, const qa_ui_resource_reference *reference) {
  font_entry *entry = font_slot(hud, reference->path, reference->point_size);
  if (!entry) qa_range_error("HUD font declaration has no completed registration");
  return entry->font;
}
static void load_current(qa_q3_pres_mission_hud *hud) {
  opened(hud);
  if (hud->load_generation != hud->generation) qa_range_error("Mission HUD menu load belongs to a retired lifecycle");
}
static bool registration_sink(void *ctx, const qa_ui_menu_registration_event *event, double *out_handle) {
  qa_q3_pres_mission_hud *hud = ctx;
  load_current(hud);
  opened(hud);
  double generation = hud->generation;
  const char *path = event->reference->path;
  bool has_result = true;
  switch (event->kind) {
    case QA_UI_RESOURCE_FONT:
      register_font(hud, path, event->reference->point_size);
      has_result = false;
      break;
    case QA_UI_RESOURCE_SHADER: {
      map_entry *entry = register_picture(hud, path);
      registration_current(hud, generation);
      *out_handle = entry->defined ? entry->picture.material.order : 0;
      break;
    }
    case QA_UI_RESOURCE_SOUND: {
      map_entry *entry = register_sound(hud, path);
      registration_current(hud, generation);
      *out_handle = hud->media->sound_bank.index_for_sound(hud->media->sound_bank.ctx, entry->sound);
      break;
    }
    case QA_UI_RESOURCE_MODEL: {
      map_entry *entry = register_model(hud, path);
      registration_current(hud, generation);
      *out_handle = hud->media->resources.model_handle(hud->media->resources.ctx, &entry->model);
      break;
    }
  }
  load_current(hud);
  return has_result;
}
static void asset_sink(void *ctx, const qa_ui_menu_asset_publication *event) {
  qa_q3_pres_mission_hud *hud = ctx;
  load_current(hud);
  if (event->field != QA_UI_ASSET_SMALL_FONT && event->field != QA_UI_ASSET_TEXT_FONT && event->field != QA_UI_ASSET_BIG_FONT) return;
  const qa_q3_registered_font *font = font_for(hud, event->reference);
  if (!font) return;
  if (event->field == QA_UI_ASSET_SMALL_FONT) hud->small_font = *font;
  else if (event->field == QA_UI_ASSET_TEXT_FONT) hud->normal_font = *font;
  else hud->big_font = *font;
}
/* ---- runtime resources ---- */
static double res_picture_handle(void *ctx, const qa_picture_asset *picture) {
  opened(ctx);
  if (!picture) return 0;
  if (picture->kind != QA_PICTURE_MATERIAL) qa_range_error("Source HUD picture handle requires a registered renderer material");
  return picture->material.order;
}
static bool res_picture_for_handle(void *ctx, double handle, qa_picture_asset *out) {
  qa_q3_pres_mission_hud *hud = ctx;
  opened(hud);
  double generation = hud->generation;
  if (handle == 0) return false;
  *out = picture_of(hud, hud->media->resources.shader_for_handle(hud->media->resources.ctx, handle));
  registration_current(hud, generation);
  return true;
}
static qa_q3_pres_scene_model res_model_for_handle(void *ctx, double handle) {
  qa_q3_pres_mission_hud *hud = ctx;
  opened(hud);
  double generation = hud->generation;
  qa_q3_pres_scene_model model = hud->media->resources.model_for_handle(hud->media->resources.ctx, handle);
  registration_current(hud, generation);
  return model;
}
static void res_register_font(void *ctx, const char *path, double point_size) { register_font(ctx, path, point_size); }
static bool res_register_picture(void *ctx, const char *path, qa_picture_asset *out) {
  map_entry *entry = register_picture(ctx, path);
  if (entry->defined) *out = entry->picture;
  return entry->defined;
}
static bool res_registered_picture(void *ctx, const char *path, qa_picture_asset *out) {
  qa_q3_pres_mission_hud *hud = ctx;
  opened(hud);
  map_entry *entry = map_slot(hud->pictures, path, false);
  if (!entry || !entry->defined) return false;
  *out = entry->picture;
  return true;
}
static bool res_register_sound(void *ctx, const char *path, qa_pcm_sound *out) {
  map_entry *entry = register_sound(ctx, path);
  if (entry->defined) *out = *entry->sound;
  return entry->defined;
}
static bool res_registered_sound(void *ctx, const char *path, qa_pcm_sound *out) {
  qa_q3_pres_mission_hud *hud = ctx;
  opened(hud);
  map_entry *entry = map_slot(hud->sounds, path, false);
  if (!entry || !entry->defined) return false;
  *out = *entry->sound;
  return true;
}
static qa_q3_pres_scene_model res_register_model(void *ctx, const char *path) { return register_model(ctx, path)->model; }
static bool res_registered_model(void *ctx, const char *path, qa_q3_pres_scene_model *out) {
  qa_q3_pres_mission_hud *hud = ctx;
  opened(hud);
  map_entry *entry = map_slot(hud->models, path, false);
  if (!entry || !entry->defined) return false;
  *out = entry->model;
  return true;
}
static qa_ui_cinematic_asset res_prepare_cinematic(void *ctx, const char *path) {
  qa_q3_pres_mission_hud *hud = ctx;
  const qa_ui_cinematic_asset *asset = NULL;
  if (!hud->host.cinematics.prepare(hud->host.cinematics.owner_ctx, path, &asset)) oom();
  return *asset;
}
/* ---- script sources ---- */
static void *keep_buffer(qa_q3_pres_mission_hud *hud, void *buffer) {
  void **buffers = realloc(hud->load_buffers, (hud->load_buffer_count + 1) * sizeof *buffers);
  if (!buffers) oom();
  hud->load_buffers = buffers;
  hud->load_buffers[hud->load_buffer_count++] = buffer;
  return buffer;
}
static void release_buffers(qa_q3_pres_mission_hud *hud) {
  for (size_t i = 0; i < hud->load_buffer_count; i++) free(hud->load_buffers[i]);
  free(hud->load_buffers);
  hud->load_buffers = NULL;
  hud->load_buffer_count = 0;
}
static bool read_asset(qa_q3_pres_mission_hud *hud, const char *path, uint8_t **bytes, size_t *length) {
  return hud->host.assets.read_sync(hud->host.assets.base.ctx, path, bytes, length);
}
/* The returned text is malloc'd; the caller owns it (keep_buffer or free). */
static qa_ui_script_source script_source(const char *path, const uint8_t *bytes, size_t length) {
  const uint8_t *nul = length ? memchr(bytes, 0, length) : NULL;
  qa_ui_script_source source;
  memset(&source, 0, sizeof source);
  source.path = path;
  size_t compressed_length = 0;
  source.text = qa_ui_compress_script_text(bytes, nul ? (size_t)(nul - bytes) : length, &compressed_length);
  if (source.text == NULL) oom();
  source.text_length = compressed_length;
  return source;
}
static bool load_source(qa_q3_pres_mission_hud *hud, const char *path, qa_ui_script_source *out) {
  if (!hud->host.assets.base.has(hud->host.assets.base.ctx, path)) return false;
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!read_asset(hud, path, &bytes, &length)) oom();
  keep_buffer(hud, bytes);
  char *owned_path = strdup(path);
  if (!owned_path) oom();
  keep_buffer(hud, owned_path);
  *out = script_source(owned_path, bytes, length);
  keep_buffer(hud, (void *)out->text);
  return true;
}
static bool resolve_root(void *ctx, const char *path, qa_ui_script_source *out) {
  qa_q3_pres_mission_hud *hud = ctx;
  if (strcmp(path, hud->load_path) == 0) {
    *out = hud->load_root;
    return true;
  }
  return load_source(hud, path, out);
}
/* TS posix.dirname and posix.join(dirname, requested). Returns malloc'd text. */
static char *posix_join_dirname(const char *from, const char *requested) {
  size_t from_length = strlen(from);
  while (from_length > 1 && from[from_length - 1] == '/') from_length--;
  size_t dir_length = from_length;
  while (dir_length > 0 && from[dir_length - 1] != '/') dir_length--;
  while (dir_length > 1 && from[dir_length - 1] == '/') dir_length--;
  const char *dir = from;
  if (dir_length == 0) {
    dir = ".";
    dir_length = 1;
  }
  size_t requested_length = strlen(requested);
  char *joined = malloc(dir_length + requested_length + 2);
  if (!joined) oom();
  memcpy(joined, dir, dir_length);
  joined[dir_length] = '/';
  memcpy(joined + dir_length + 1, requested, requested_length + 1);
  bool absolute = joined[0] == '/';
  bool trailing = requested_length > 0 && requested[requested_length - 1] == '/';
  char *result = malloc(strlen(joined) + 3);
  if (!result) oom();
  size_t out = 0, kept_segments = 0;
  if (absolute) result[out++] = '/';
  size_t base = out;
  const char *cursor = joined;
  while (*cursor) {
    while (*cursor == '/') cursor++;
    const char *end = cursor;
    while (*end && *end != '/') end++;
    size_t length = (size_t)(end - cursor);
    if (length == 0) break;
    if (length == 1 && cursor[0] == '.') {
      /* normalized away */
    } else if (length == 2 && cursor[0] == '.' && cursor[1] == '.') {
      if (kept_segments > 0) {
        while (out > base && result[out - 1] != '/') out--;
        if (out > base) out--;
        kept_segments--;
      } else if (!absolute) {
        if (out > base) result[out++] = '/';
        result[out++] = '.';
        result[out++] = '.';
        base = out;
      }
    } else {
      if (out > base || (out == base && base > (absolute ? 1u : 0u))) result[out++] = '/';
      memcpy(result + out, cursor, length);
      out += length;
      kept_segments++;
    }
    cursor = end;
  }
  if (out == 0) result[out++] = '.';
  if (trailing && result[out - 1] != '/') result[out++] = '/';
  result[out] = '\0';
  free(joined);
  return result;
}
static bool resolve_include(void *ctx, const qa_ui_script_include_request *request, qa_ui_script_source *out) {
  qa_q3_pres_mission_hud *hud = ctx;
  char *joined = posix_join_dirname(request->from_path, request->requested_path);
  bool found = load_source(hud, joined, out);
  free(joined);
  return found || load_source(hud, request->requested_path, out);
}
static double load_random(void *ctx) { return qa_game_random_rand(((qa_q3_pres_mission_hud *)ctx)->host.random); }
/* ---- runtime bindings ---- */
static double rt_cvar_value(void *ctx, const char *name) {
  qa_q3_pres_mission_hud *hud = ctx;
  qa_cvar_snapshot snapshot;
  if (!qa_cvar_registry_get(hud->host.cvars, name, &snapshot)) return qa_game_atof("");
  char text[128];
  snprintf(text, sizeof text, "%.127s", snapshot.value);
  qa_cvar_read_free(&snapshot);
  return qa_game_atof(text);
}
/* TS runtime reads hud fonts and widget assets through live getters. */
static void publish_live_options(qa_q3_pres_mission_hud *hud) {
  if (!hud->runtime) return;
  qa_ui_runtime_set_fonts(hud->runtime, qa_q3_pres_mission_hud_fonts(hud));
  qa_ui_runtime_set_widget_assets(hud->runtime, &hud->widget_assets);
}
static double feeder_count_cb(void *ctx, double feeder) { return qa_q3_pres_mission_hud_feeder_count(ctx, feeder); }
static bool feeder_item_cb(void *ctx, double feeder, double index, double column, qa_ui_runtime_feeder_item *out) {
  qa_q3_pres_mission_hud_feeder_item(ctx, feeder, index, column, out);
  return true;
}
/* CG_FeederItemImage returns handle zero; text-column optional images are a separate callback. */
static bool feeder_image_cb(void *ctx, double feeder, double index, qa_picture_asset *out) {
  (void)ctx; (void)feeder; (void)index; (void)out;
  return false;
}
static void feeder_select_cb(void *ctx, double feeder, double index) { qa_q3_pres_mission_hud_feeder_selection(ctx, feeder, index); }
static bool od_visible(void *ctx, double flags) { return qa_q3_pres_mission_owner_draw_visible(&((qa_q3_pres_mission_hud *)ctx)->owner_draw, flags); }
static double od_width(void *ctx, double id, double scale) { return qa_q3_pres_mission_owner_draw_width(&((qa_q3_pres_mission_hud *)ctx)->owner_draw, id, scale); }
static double od_value(void *ctx, double id) { return qa_q3_pres_mission_owner_draw_value(&((qa_q3_pres_mission_hud *)ctx)->owner_draw, id); }
static qa_ui_owner_draw_key_result od_key(void *ctx, double id, double flags, double special, double key) {
  (void)ctx; (void)id; (void)flags; (void)key;
  qa_ui_owner_draw_key_result result = {false, special};
  return result;
}
static void od_paint(void *ctx, const qa_ui_owner_draw_paint_request *request) {
  qa_q3_pres_mission_hud *hud = ctx;
  qa_q3_pres_mission_owner_draw_paint(&hud->owner_draw, request);
}
static void od_close_cinematic(void *ctx, double handle) {
  qa_q3_pres_mission_hud *hud = ctx;
  hud->host.cinematics.stop_slot(hud->host.cinematics.owner_ctx, handle);
}
/* CG_RunMenuScript is empty in the original cgame module. */
static void external_script_run(void *ctx, qa_ui_script_cursor *cursor, qa_ui_external_script_context context) {
  (void)ctx; (void)cursor; (void)context;
}
static qa_vec4 team_color_cb(void *ctx) {
  double team = qa_q3_base_player_state_slots_get(&snapshot_of(ctx)->persistant, QA_Q3_BASE_PERS_TEAM);
  qa_vec4 color = {0, (float)0.17, 0, 0.25};
  if (team == QA_Q3_BASE_TEAM_RED) { color.x = 1; color.y = 0; }
  else if (team == QA_Q3_BASE_TEAM_BLUE) { color.y = 0; color.z = 1; }
  return color;
}
static qa_ui_runtime_resources runtime_resources(qa_q3_pres_mission_hud *hud) {
  qa_ui_runtime_resources resources;
  memset(&resources, 0, sizeof resources);
  resources.handles.kind = QA_UI_HANDLES_SOURCE;
  resources.handles.ctx = hud;
  resources.handles.picture_handle = res_picture_handle;
  resources.handles.picture_for_handle = res_picture_for_handle;
  resources.handles.model_for_handle = res_model_for_handle;
  resources.ctx = hud;
  resources.register_font = res_register_font;
  resources.register_picture = res_register_picture;
  resources.registered_picture = res_registered_picture;
  resources.register_sound = res_register_sound;
  resources.registered_sound = res_registered_sound;
  resources.register_model = res_register_model;
  resources.registered_model = res_registered_model;
  resources.prepare_cinematic = res_prepare_cinematic;
  return resources;
}
static qa_ui_runtime *create_runtime(qa_q3_pres_mission_hud *hud, const qa_ui_menu_definitions *definitions) {
  qa_ui_runtime_options options;
  memset(&options, 0, sizeof options);
  options.definitions = *definitions;
  options.cvar_value = rt_cvar_value;
  options.cvar_value_ctx = hud;
  options.cvars = hud->host.cvars;
  options.resources = runtime_resources(hud);
  options.fonts = *qa_q3_pres_mission_hud_fonts(hud);
  options.widget_assets = hud->widget_assets;
  options.zero_picture = picture_of(hud, NULL);
  options.audio = hud->host.audio;
  options.cinematics = hud->host.cinematics.base;
  options.paint_model = qa_q3_pres_engine_ui_model_painter_paint_ctx;
  options.paint_model_ctx = hud->host.model_painter;
  options.feeder.ctx = hud;
  options.feeder.count = feeder_count_cb;
  options.feeder.item = feeder_item_cb;
  options.feeder.image = feeder_image_cb;
  options.feeder.select = feeder_select_cb;
  options.owner_draw.ctx = hud;
  options.owner_draw.visible = od_visible;
  options.owner_draw.width = od_width;
  options.owner_draw.value = od_value;
  options.owner_draw.handle_key = od_key;
  options.owner_draw.paint = od_paint;
  options.owner_draw.close_cinematic = od_close_cinematic;
  options.external_script.ctx = hud;
  options.external_script.run = external_script_run;
  options.get_team_color = team_color_cb;
  options.get_team_color_ctx = hud;
  return qa_ui_runtime_create(&options);
}
char *qa_q3_pres_mission_hud_get_menu_buffer(qa_q3_pres_mission_hud *hud, const char *filename) {
  opened(hud);
  char message[512];
  if (!hud->host.assets.base.has(hud->host.assets.base.ctx, filename)) {
    snprintf(message, sizeof message, "^1menu file not found: %s, using default\n", filename);
    print_text(hud, message);
    return NULL;
  }
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!read_asset(hud, filename, &bytes, &length)) return NULL;
  if (length >= MENU_BUFFER_LIMIT) {
    snprintf(message, sizeof message, "^1menu file too large: %s is %zu, max allowed is 32768", filename, length);
    print_text(hud, message);
    free(bytes);
    return NULL;
  }
  qa_ui_script_source source = script_source(filename, bytes, length);
  char *text = malloc(source.text_length + 1);
  if (text) {
    memcpy(text, source.text, source.text_length);
    text[source.text_length] = '\0';
  }
  free((void *)source.text);
  free(bytes);
  return text;
}
void qa_q3_pres_mission_hud_reset_strings(qa_q3_pres_mission_hud *hud) {
  opened(hud);
  hud->generation++;
  qa_ui_team_arena_memory_initialize_strings(hud->memory);
  if (hud->runtime) qa_ui_runtime_reset_definitions(hud->runtime, QA_UI_RUNTIME_RESET_STRINGS);
}
void qa_q3_pres_mission_hud_reset_menus(qa_q3_pres_mission_hud *hud) {
  opened(hud);
  hud->generation++;
  if (hud->runtime) qa_ui_runtime_reset_definitions(hud->runtime, QA_UI_RUNTIME_RESET_MENUS);
}
bool qa_q3_pres_mission_hud_load_hud_menu(qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_mission_hud_reset_menus(hud);
  char requested[1024] = "";
  qa_cvar_snapshot snapshot;
  if (qa_cvar_registry_get(hud->host.cvars, "cg_hudFiles", &snapshot)) {
    snprintf(requested, sizeof requested, "%.1023s", snapshot.value);
    qa_cvar_read_free(&snapshot);
  }
  return qa_q3_pres_mission_hud_load_menus(hud, requested[0] == '\0' ? "ui/hud.txt" : requested);
}
bool qa_q3_pres_mission_hud_load_menus(qa_q3_pres_mission_hud *hud, const char *path) {
  opened(hud);
  if (hud->loading) qa_range_error("Mission HUD menu registration is already active");
  double started = hud->host.milliseconds(hud->host.ctx);
  if (!hud->host.assets.base.has(hud->host.assets.base.ctx, path))
    qa_range_error("^3menu file not found: %s, using default\n", path);
  hud->load_generation = hud->generation;
  hud->loading = true;
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!read_asset(hud, path, &bytes, &length)) {
    hud->loading = false;
    return false;
  }
  keep_buffer(hud, bytes);
  load_current(hud);
  if (length >= MENU_ROOT_LIMIT) qa_range_error("^1menu file too large: %s is %zu, max allowed is 4096", path, length);
  char *owned_path = strdup(path);
  if (!owned_path) oom();
  keep_buffer(hud, owned_path);
  hud->load_path = owned_path;
  hud->load_root = script_source(owned_path, bytes, length);
  keep_buffer(hud, (void *)hud->load_root.text);
  qa_q3_pres_mission_hud_reset_menus(hud);
  hud->load_generation = hud->generation;
  qa_ui_menu_parse_host parse_host;
  memset(&parse_host, 0, sizeof parse_host);
  parse_host.kind = QA_UI_PARSE_HOST_RESOLVED;
  parse_host.resolver.resolve_root = resolve_root;
  parse_host.resolver.resolve_root_ctx = hud;
  parse_host.resolver.resolve = resolve_include;
  parse_host.resolver.resolve_ctx = hud;
  parse_host.random = load_random;
  parse_host.random_ctx = hud;
  qa_ui_menu_load_plan plan = {owned_path};
  qa_ui_menu_parse_options parse_options;
  memset(&parse_options, 0, sizeof parse_options);
  parse_options.has_memory = true;
  parse_options.memory.kind = QA_UI_MENU_MEMORY_QVM32;
  parse_options.memory.memory = hud->memory;
  parse_options.registration_sink = registration_sink;
  parse_options.registration_sink_ctx = hud;
  parse_options.asset_sink = asset_sink;
  parse_options.asset_sink_ctx = hud;
  parse_options.initial_assets = hud->asset_definitions;
  qa_ui_menu_definitions *definitions = calloc(1, sizeof *definitions);
  if (!definitions) oom();
  qa_ui_load_menu_definitions(&parse_host, &plan, NULL, &parse_options, definitions);
  load_current(hud);
  qa_ui_menu_definitions **all = realloc(hud->definitions, (hud->definition_count + 1) * sizeof *all);
  if (!all) oom();
  hud->definitions = all;
  hud->definitions[hud->definition_count++] = definitions;
  hud->asset_definitions = &definitions->assets;
  if (definitions->assets.gradient_bar) {
    map_entry *entry = map_slot(hud->pictures, definitions->assets.gradient_bar->path, false);
    hud->widget_assets.gradient_bar = entry && entry->defined ? entry->picture : picture_of(hud, NULL);
  }
  if (hud->runtime) {
    publish_live_options(hud);
    qa_ui_runtime_reload_definitions(hud->runtime, definitions);
    load_current(hud);
  } else {
    qa_ui_runtime *runtime = create_runtime(hud, definitions);
    if (!runtime) oom();
    if (hud->closed || hud->load_generation != hud->generation) {
      qa_ui_runtime_retire(runtime);
      load_current(hud);
    }
    hud->runtime = runtime;
  }
  char message[64];
  snprintf(message, sizeof message, "UI menu load time = %d milli seconds\n", qa_int32(hud->host.milliseconds(hud->host.ctx) - started));
  print_text(hud, message);
  release_buffers(hud);
  hud->load_path = NULL;
  hud->loading = false;
  return true;
}
void qa_q3_pres_mission_hud_init_team_chat(qa_q3_pres_mission_hud *hud) {
  hud->system_chat[0] = hud->team_chat1[0] = hud->team_chat2[0] = '\0';
}
void qa_q3_pres_mission_hud_set_print_string(qa_q3_pres_mission_hud *hud, double type, const char *value) {
  source_text(value, 256);
  if (type == 0) {
    strcpy(hud->system_chat, value);
  } else {
    strcpy(hud->team_chat2, hud->team_chat1);
    strcpy(hud->team_chat1, value);
  }
}
qa_q3_pres_mission_hud_chat qa_q3_pres_mission_hud_chat_text(const qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_mission_hud_chat chat = {hud->system_chat, hud->team_chat1, hud->team_chat2};
  return chat;
}
void qa_q3_pres_mission_hud_check_order_pending(qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  if (cgs->game_type < QA_Q3_BASE_GT_CTF || !cgs->order_pending) return;
  double order_index = cgs->current_order - 1;
  const team_order *order = order_index == trunc(order_index) && order_index >= 0 && order_index < ORDER_COUNT ? &ORDERS[(size_t)order_index] : NULL;
  double selected = selected_index(hud);
  char text[128];
  if (selected == hud->state->num_sorted_team_players) {
    if (!order) qa_range_error("CG_CheckOrderPending: Everyone order has no source voice command");
    snprintf(text, sizeof text, "cmd vsay_team %s\n", order->team);
    append_command(hud, text);
  } else {
    check_at(selected, 32);
    double client = hud->state->sorted_team_players[(size_t)selected];
    if (client == snapshot_of(hud)->client_num && order) {
      snprintf(text, sizeof text, "teamtask %s\n", QA_JS_NUMBER(cgs->current_order));
      append_command(hud, text);
      snprintf(text, sizeof text, "cmd vsay_team %s\n", order->personal);
      append_command(hud, text);
    } else if (order) {
      snprintf(text, sizeof text, "cmd vtell %s %s\n", QA_JS_NUMBER(client), order->team);
      append_command(hud, text);
    }
  }
  if (order && order->button) append_command(hud, order->button);
  cgs->order_pending = false;
}
static void set_selected_player_name(qa_q3_pres_mission_hud *hud) {
  double index = selected_index(hud);
  if (index >= 0 && index < hud->state->num_sorted_team_players) {
    check_at(index, 32);
    double number = hud->state->sorted_team_players[(size_t)index];
    check_at(number, QA_Q3_PRES_MAX_CLIENTS);
    qa_q3_pres_client_info *client = &hud->static_state->client_info[(size_t)number];
    set_cvar(hud, "cg_selectedPlayerName", client->name ? client->name : "");
    char text[32];
    snprintf(text, sizeof text, "%s", QA_JS_NUMBER(number));
    set_cvar(hud, "cg_selectedPlayer", text);
    hud->static_state->current_order = client->team_task;
  } else {
    set_cvar(hud, "cg_selectedPlayerName", "Everyone");
  }
}
double qa_q3_pres_mission_hud_get_selected_player(qa_q3_pres_mission_hud *hud) {
  double index = selected_index(hud);
  if (index < 0 || index >= hud->state->num_sorted_team_players) set_selected(hud, 0);
  return selected_index(hud);
}
void qa_q3_pres_mission_hud_select_next_player(qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_mission_hud_check_order_pending(hud);
  double index = selected_index(hud);
  set_selected(hud, index >= 0 && index < hud->state->num_sorted_team_players ? qa_int32(index + 1) : 0);
  set_selected_player_name(hud);
}
void qa_q3_pres_mission_hud_select_previous_player(qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_mission_hud_check_order_pending(hud);
  double index = selected_index(hud);
  set_selected(hud, index > 0 && index < hud->state->num_sorted_team_players ? qa_int32(index - 1) : hud->state->num_sorted_team_players);
  set_selected_player_name(hud);
}
static qa_q3_pres_client_score *score_at(qa_q3_pres_mission_hud *hud, double index) {
  check_at(index, QA_Q3_PRES_MAX_CLIENTS);
  return &hud->state->scores[(size_t)index];
}
double qa_q3_pres_mission_hud_feeder_count(qa_q3_pres_mission_hud *hud, double feeder) {
  if (feeder == QA_Q3_PRES_FEEDER_SCOREBOARD) return hud->state->num_scores;
  double team;
  if (feeder == QA_Q3_PRES_FEEDER_RED) team = QA_Q3_BASE_TEAM_RED;
  else if (feeder == QA_Q3_PRES_FEEDER_BLUE) team = QA_Q3_BASE_TEAM_BLUE;
  else return 0;
  double count = 0;
  for (double i = 0; i < hud->state->num_scores; i++)
    if (score_at(hud, i)->team == team) count++;
  return count;
}
void qa_q3_pres_mission_hud_feeder_item(qa_q3_pres_mission_hud *hud, double feeder, double index, double column,
                                        qa_ui_runtime_feeder_item *out) {
  double team = feeder == QA_Q3_PRES_FEEDER_RED ? QA_Q3_BASE_TEAM_RED : feeder == QA_Q3_PRES_FEEDER_BLUE ? QA_Q3_BASE_TEAM_BLUE : -1;
  double score_index = index;
  if (hud->static_state->game_type >= QA_Q3_BASE_GT_TEAM) {
    double count = 0;
    for (double i = 0; i < hud->state->num_scores; i++) {
      if (score_at(hud, i)->team != team) continue;
      if (count == index) { score_index = i; break; }
      count++;
    }
  }
  qa_q3_pres_client_score *score = score_at(hud, score_index);
  check_at(score->client, QA_Q3_PRES_MAX_CLIENTS);
  qa_q3_pres_client_info *info = &hud->static_state->client_info[(size_t)score->client];
  hud->feeder_text[0] = '\0';
  memset(out, 0, sizeof *out);
  out->text = hud->feeder_text;
  if (!info->info_valid) return;
  int32_t powerups = qa_int32(info->powerups);
  char *text = hud->feeder_text;
  size_t size = sizeof hud->feeder_text;
  if (column == 0) {
    qa_q3_powerup powerup = QA_Q3_PW_NONE;
    if (powerups & (1 << QA_Q3_PW_NEUTRALFLAG)) powerup = QA_Q3_PW_NEUTRALFLAG;
    else if (powerups & (1 << QA_Q3_PW_REDFLAG)) powerup = QA_Q3_PW_REDFLAG;
    else if (powerups & (1 << QA_Q3_PW_BLUEFLAG)) powerup = QA_Q3_PW_BLUEFLAG;
    if (powerup != QA_Q3_PW_NONE) {
      const qa_q3_base_item_definition *item = qa_q3_base_find_item_for_powerup(hud->state->product, powerup);
      if (!item) qa_range_error("Mission HUD flag has no source item");
      size_t list_count = 0, visual_count = 0;
      const qa_q3_base_item_definition *list = qa_q3_base_item_list(hud->state->product, &list_count);
      const qa_q3_pres_packet_item_visual *visuals = qa_q3_pres_client_weapon_media_registry_items(hud->host.weapon_registry, &visual_count);
      double item_index = (double)(item - list);
      check_at(item_index, (double)visual_count);
      out->has_picture = true;
      out->picture = picture_of(hud, visuals[(size_t)item_index].icon);
    } else if (info->bot_skill > 0 && info->bot_skill <= 5) {
      check_at(info->bot_skill - 1, 5);
      out->has_picture = true;
      out->picture = picture_of(hud, hud->media->graphics.bot_skill_shaders[(size_t)(info->bot_skill - 1)]);
    } else if (info->handicap < 100) {
      snprintf(text, size, "%s", QA_JS_NUMBER(info->handicap));
    }
  } else if (column == 1) {
    if (team != -1) {
      out->has_picture = true;
      out->picture = picture_of(hud, qa_q3_pres_mission_owner_draw_status_handle(&hud->owner_draw, info->team_task));
    }
  } else if (column == 2) {
    const qa_q3_base_stat_schema *stats = qa_q3_base_stat_schema_for(hud->state->product);
    int32_t ready = qa_int32(qa_q3_base_player_state_slots_get(&snapshot_of(hud)->stats, stats->clients_ready));
    if (ready & (int32_t)(1u << (qa_int32(score->client) & 31))) {
      snprintf(text, size, "Ready");
    } else if (team == -1) {
      if (hud->static_state->game_type == QA_Q3_BASE_GT_TOURNAMENT) snprintf(text, size, "%s/%s", QA_JS_NUMBER(info->wins), QA_JS_NUMBER(info->losses));
      else if (info->team == QA_Q3_BASE_TEAM_SPECTATOR) snprintf(text, size, "Spectator");
    } else if (info->team_leader) {
      snprintf(text, size, "Leader");
    }
  } else if (column == 3) {
    out->text = info->name ? info->name : "";
  } else if (column == 4) {
    snprintf(text, size, "%s", QA_JS_NUMBER(info->score));
  } else if (column == 5) {
    snprintf(text, size, "%4s", QA_JS_NUMBER(score->time)); /* `${time}`.padStart(4) */
  } else if (column == 6) {
    if (score->ping == -1) snprintf(text, size, "connecting");
    else snprintf(text, size, "%4s", QA_JS_NUMBER(score->ping));
  }
}
void qa_q3_pres_mission_hud_feeder_selection(qa_q3_pres_mission_hud *hud, double feeder, double index) {
  if (hud->static_state->game_type < QA_Q3_BASE_GT_TEAM) {
    hud->state->selected_score = index;
    return;
  }
  double team = feeder == QA_Q3_PRES_FEEDER_RED ? QA_Q3_BASE_TEAM_RED : QA_Q3_BASE_TEAM_BLUE;
  double count = 0;
  for (double i = 0; i < hud->state->num_scores; i++) {
    if (score_at(hud, i)->team != team) continue;
    if (index == count) hud->state->selected_score = i;
    count++;
  }
}
static void score_selection_scan(qa_q3_pres_mission_hud *hud, double *red, double *blue) {
  qa_q3_base_source_player_state *player = snapshot_of(hud);
  *red = *blue = 0;
  for (double i = 0; i < hud->state->num_scores; i++) {
    qa_q3_pres_client_score *score = score_at(hud, i);
    if (score->team == QA_Q3_BASE_TEAM_RED) (*red)++;
    else if (score->team == QA_Q3_BASE_TEAM_BLUE) (*blue)++;
    if (player->client_num == score->client) hud->state->selected_score = i;
  }
}
void qa_q3_pres_mission_hud_set_score_selection(qa_q3_pres_mission_hud *hud) {
  double red, blue;
  score_selection_scan(hud, &red, &blue);
}
void qa_q3_pres_mission_hud_set_score_selection_menu(qa_q3_pres_mission_hud *hud, qa_ui_captured_menu *menu) {
  double red, blue;
  score_selection_scan(hud, &red, &blue);
  if (!hud->runtime) return;
  if (hud->static_state->game_type >= QA_Q3_BASE_GT_TEAM) {
    bool is_blue = score_at(hud, hud->state->selected_score)->team == QA_Q3_BASE_TEAM_BLUE;
    qa_ui_runtime_set_captured_feeder_selection(hud->runtime, menu, is_blue ? QA_Q3_PRES_FEEDER_BLUE : QA_Q3_PRES_FEEDER_RED, is_blue ? blue : red);
    return;
  }
  qa_ui_runtime_set_captured_feeder_selection(hud->runtime, menu, QA_Q3_PRES_FEEDER_SCOREBOARD, hud->state->selected_score);
}
void qa_q3_pres_mission_hud_clear_scoreboard(qa_q3_pres_mission_hud *hud) { hud->scoreboard = NULL; }
qa_ui_captured_menu *qa_q3_pres_mission_hud_menu_scoreboard(qa_q3_pres_mission_hud *hud) { return hud->scoreboard; }
void qa_q3_pres_mission_hud_scroll_feeder(qa_q3_pres_mission_hud *hud, qa_ui_captured_menu *menu, double feeder, bool down) {
  if (menu != hud->scoreboard) qa_range_error("Score scrolling requires the current Mission HUD scoreboard");
  opened(hud);
  if (!hud->runtime) qa_range_error("Mission HUD menus are not loaded");
  qa_ui_runtime_scroll_captured_feeder(hud->runtime, menu, feeder, down);
}
/* cgDC.realTime/frameTime are never assigned by the original cgame module. */
static qa_ui_runtime_frame hud_frame(qa_q3_pres_mission_hud *hud) {
  qa_ui_runtime_frame frame = {0, 0, &hud->host.icons->tools->draw};
  return frame;
}
void qa_q3_pres_mission_hud_paint_all(qa_q3_pres_mission_hud *hud) {
  opened(hud);
  publish_live_options(hud);
  if (hud->runtime) qa_ui_runtime_run_frame(hud->runtime, hud_frame(hud), 0);
}
static void reset_player_cb(void *ctx, qa_q3_pres_client_entity *entity) {
  qa_q3_pres_mission_hud *hud = ctx;
  hud->host.reset_player_entity(hud->host.ctx, entity);
}
bool qa_q3_pres_mission_hud_draw_scoreboard(qa_q3_pres_mission_hud *hud) {
  opened(hud);
  qa_q3_pres_client_game_state *state = hud->state;
  double type = state->predicted_player_state.pm_type;
  if (hud->scoreboard && hud->runtime) qa_ui_runtime_clear_captured_forced(hud->runtime, hud->scoreboard);
  if (vm_integer(hud, "cl_paused") != 0 ||
      (hud->static_state->game_type == QA_Q3_BASE_GT_SINGLE_PLAYER && type == QA_Q3_PM_INTERMISSION)) {
    state->deferred_player_loading = 0;
    hud->scoreboard_first_time = true;
    return false;
  }
  if (state->warmup != 0 && !state->show_scores) return false;
  qa_vec4 fade;
  if (!state->show_scores && type != QA_Q3_PM_DEAD && type != QA_Q3_PM_INTERMISSION &&
      !qa_q3_pres_fade_color(state->time, state->score_fade_time, 200, &fade)) {
    state->deferred_player_loading = 0;
    if (!qa_q3_pres_set_string(&state->killer_name, "")) oom();
    hud->scoreboard_first_time = true;
    return false;
  }
  if (!hud->scoreboard && hud->runtime)
    hud->scoreboard = qa_ui_runtime_menu_handle(hud->runtime, hud->static_state->game_type >= QA_Q3_BASE_GT_TEAM ? "teamscore_menu" : "score_menu");
  if (hud->scoreboard && hud->runtime) {
    if (hud->scoreboard_first_time) {
      qa_q3_pres_mission_hud_set_score_selection_menu(hud, hud->scoreboard);
      opened(hud);
      hud->scoreboard_first_time = false;
    }
    publish_live_options(hud);
    qa_ui_runtime_paint_captured(hud->runtime, hud->scoreboard, hud_frame(hud), true);
  }
  state->deferred_player_loading = qa_int32(state->deferred_player_loading + 1);
  if (state->deferred_player_loading > 10 &&
      !qa_q3_pres_client_info_store_load_deferred_players(hud->host.clients, reset_player_cb, hud))
    oom();
  return true;
}
void qa_q3_pres_mission_hud_close_by_name(qa_q3_pres_mission_hud *hud, const char *name) {
  opened(hud);
  if (hud->runtime) (void)qa_ui_runtime_close(hud->runtime, name);
}
void qa_q3_pres_mission_hud_show_response_head(qa_q3_pres_mission_hud *hud) {
  opened(hud);
  if (hud->runtime) (void)qa_ui_runtime_show(hud->runtime, "voiceMenu");
  set_cvar(hud, "cl_conXOffset", "72");
  hud->state->voice_time = hud->state->time;
}
void qa_q3_pres_mission_hud_draw_timed_menus(qa_q3_pres_mission_hud *hud) {
  if (hud->state->voice_time != 0 && qa_int32(hud->state->time - hud->state->voice_time) > 2500) {
    qa_q3_pres_mission_hud_close_by_name(hud, "voiceMenu");
    set_cvar(hud, "cl_conXOffset", "0");
    hud->state->voice_time = 0;
  }
}
double qa_q3_pres_mission_hud_client_num_from_name(qa_q3_pres_mission_hud *hud, const char *name) {
  char *text = folded_copy(name);
  double found = -1;
  for (double i = 0; i < hud->static_state->maxclients && found < 0; i++) {
    check_at(i, QA_Q3_PRES_MAX_CLIENTS);
    qa_q3_pres_client_info *client = &hud->static_state->client_info[(size_t)i];
    if (!client->info_valid) continue;
    char *candidate = folded_copy(client->name ? client->name : "");
    if (strcmp(candidate, text) == 0) found = i;
    free(candidate);
  }
  free(text);
  return found;
}
void qa_q3_pres_mission_hud_hide_team_menu(qa_q3_pres_mission_hud *hud) {
  qa_q3_pres_mission_hud_close_by_name(hud, "teamMenu");
  qa_q3_pres_mission_hud_close_by_name(hud, "getMenu");
}
void qa_q3_pres_mission_hud_show_team_menu(qa_q3_pres_mission_hud *hud) {
  opened(hud);
  if (hud->runtime) (void)qa_ui_runtime_show(hud->runtime, "teamMenu");
}
void qa_q3_pres_mission_hud_event_handling(qa_q3_pres_mission_hud *hud, double type) {
  hud->static_state->event_handling = qa_int32(type);
  if (type == 0) qa_q3_pres_mission_hud_hide_team_menu(hud);
}
void qa_q3_pres_mission_hud_mouse_event(qa_q3_pres_mission_hud *hud, double x, double y) {
  qa_q3_pres_client_game_static_state *cgs = hud->static_state;
  /* vmMain copies the old cgs cursor into cgDC before CG_MouseEvent applies its delta. */
  if (hud->runtime) qa_ui_runtime_set_display_cursor(hud->runtime, cgs->cursor_x, cgs->cursor_y);
  double type = hud->state->predicted_player_state.pm_type;
  if ((type == QA_Q3_PM_NORMAL || type == QA_Q3_PM_SPECTATOR) && !hud->state->show_scores) {
    hud->host.set_key_catcher(hud->host.ctx, 0);
    return;
  }
  cgs->cursor_x = qa_js_max(0, qa_js_min(640, qa_int32(cgs->cursor_x + x)));
  cgs->cursor_y = qa_js_max(0, qa_js_min(480, qa_int32(cgs->cursor_y + y)));
  publish_live_options(hud);
  qa_ui_cursor_type cursor = hud->runtime ? qa_ui_runtime_cursor_type(hud->runtime, cgs->cursor_x, cgs->cursor_y) : QA_UI_CURSOR_ARROW;
  cgs->active_cursor = cursor == QA_UI_CURSOR_ARROW ? hud->media->graphics.select_cursor : hud->media->graphics.size_cursor;
  if (!hud->runtime) return;
  if (hud->captured_menu) qa_ui_runtime_move_captured_menu(hud->runtime, hud->captured_menu, x, y);
  else (void)qa_ui_runtime_pointer_move(hud->runtime, cgs->cursor_x, cgs->cursor_y);
}
void qa_q3_pres_mission_hud_key_event(qa_q3_pres_mission_hud *hud, double key, bool down) {
  if (!down) return;
  double type = hud->state->predicted_player_state.pm_type;
  if (type == QA_Q3_PM_NORMAL || (type == QA_Q3_PM_SPECTATOR && !hud->state->show_scores)) {
    qa_q3_pres_mission_hud_event_handling(hud, 0);
    hud->host.set_key_catcher(hud->host.ctx, 0);
    return;
  }
  if (hud->runtime) {
    publish_live_options(hud);
    qa_ui_key_event event = {QA_UI_KEY_EVENT_KEY, key, down};
    (void)qa_ui_runtime_handle_key(hud->runtime, event, hud->static_state->cursor_x, hud->static_state->cursor_y);
  }
  if (hud->captured_menu) hud->captured_menu = NULL;
  else if (key == QA_KEY_MOUSE2 && hud->runtime)
    hud->captured_menu = qa_ui_runtime_capture_menu(hud->runtime, hud->static_state->cursor_x, hud->static_state->cursor_y);
}
