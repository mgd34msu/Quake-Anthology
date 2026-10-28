/* Port of src/app/bootstrap/original-save.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/original_save.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = malloc(len + 1);
  if (out == NULL) qa_range_error("Out of memory");
  memcpy(out, text, len + 1);
  return out;
}
static void set_owned(char **slot, const char *text) { free(*slot); *slot = dup_text(text); }
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
static bool ends_with_ci(const char *text, const char *suffix) {
  size_t a = strlen(text), b = strlen(suffix);
  if (a < b) return false;
  for (size_t i = 0; i < b; i++)
    if (tolower((unsigned char)text[a - b + i]) != tolower((unsigned char)suffix[i])) return false;
  return true;
}
/* Deep-copy the owned fields of qa_app_options. Borrowed/opaque
 * fields are copied as-is (cleared by strip_consumed below). */
static qa_app_options clone_options(const qa_app_options *source) {
  qa_app_options o = *source;
  o.network_transport_relay = dup_text(source->network_transport_relay);
  o.startup_commands = NULL; o.startup_command_count = 0;
  if (source->startup_command_count > 0) {
    o.startup_commands = calloc(source->startup_command_count, sizeof *o.startup_commands);
    if (o.startup_commands == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < source->startup_command_count; i++)
      o.startup_commands[i] = dup_text(source->startup_commands[i]);
    o.startup_command_count = source->startup_command_count;
  }
  o.remote_content.directory = dup_text(source->remote_content.directory);
  o.server_profile_path = dup_text(source->server_profile_path);
  o.corpus_root = dup_text(source->corpus_root);
  o.user_content_root = dup_text(source->user_content_root);
  o.addon_content_roots = NULL; o.addon_content_root_count = 0;
  if (source->addon_content_root_count > 0) {
    o.addon_content_roots = calloc(source->addon_content_root_count, sizeof *o.addon_content_roots);
    if (o.addon_content_roots == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < source->addon_content_root_count; i++) {
      o.addon_content_roots[i] = source->addon_content_roots[i];
      o.addon_content_roots[i].root = dup_text(source->addon_content_roots[i].root);
    }
    o.addon_content_root_count = source->addon_content_root_count;
  }
  o.product = dup_text(source->product);
  o.map_product = dup_text(source->map_product);
  o.map = dup_text(source->map);
  o.quakec_program = dup_text(source->quakec_program);
  o.q2_game_library = dup_text(source->q2_game_library);
  o.weapon_behavior_product = dup_text(source->weapon_behavior_product);
  o.weapon_behavior_id = dup_text(source->weapon_behavior_id);
  o.mods = NULL; o.mod_count = 0;
  if (source->mod_count > 0) {
    o.mods = calloc(source->mod_count, sizeof *o.mods);
    if (o.mods == NULL) qa_range_error("Out of memory");
    for (size_t i = 0; i < source->mod_count; i++) {
      o.mods[i].product = dup_text(source->mods[i].product);
      o.mods[i].id = dup_text(source->mods[i].id);
    }
    o.mod_count = source->mod_count;
  }
  o.movement_product = dup_text(source->movement_product);
  o.character_model = dup_text(source->character_model);
  o.character_skin = dup_text(source->character_skin);
  o.network_host = dup_text(source->network_host);
  o.network_remote = dup_text(source->network_remote);
  return o;
}
/* TS: drop movementProduct, mapProduct, q2GameLibrary,
 * weaponBehavior, teamArenaSkirmish, q3MapLaunch, q3Product,
 * botSkill, serverProfile, serverProfilePath, startupCommands,
 * explicitRules (and authoredCampaignStart earlier). */
static void strip_consumed(qa_app_options *o) {
  o->authored_campaign_start = false;
  free(o->movement_product); o->movement_product = NULL;
  free(o->map_product); o->map_product = NULL;
  free(o->q2_game_library); o->q2_game_library = NULL;
  o->has_weapon_behavior = false;
  free(o->weapon_behavior_product); o->weapon_behavior_product = NULL;
  free(o->weapon_behavior_id); o->weapon_behavior_id = NULL;
  o->team_arena_skirmish = NULL;
  o->q3_map_launch = NULL;
  o->has_q3_product = false;
  o->has_bot_skill = false;
  o->server_profile = NULL;
  free(o->server_profile_path); o->server_profile_path = NULL;
  for (size_t i = 0; i < o->startup_command_count; i++) free(o->startup_commands[i]);
  free(o->startup_commands); o->startup_commands = NULL; o->startup_command_count = 0;
  o->explicit_skill = false; o->explicit_mode = false; o->explicit_capacity = false;
}
static bool prepare_kex(const qa_content_catalog *catalog, const char *path,
    qa_app_options *load_options, const qa_app_original_save_host *host,
    qa_app_prepared_application_save *out, const char **error_out) {
  qa_app_kex_save_view save = {0};
  const char *error = host->read_kex_save(host->ctx, path, &save);
  if (error != NULL) { *error_out = error; return false; }
  if (!save.restorable) {
    *error_out = "Retail Quake II saves with recorded client or level state cannot be restored by this engine yet.";
    return false;
  }
  const qa_catalog_discovered_product *product = NULL;
  const char *wanted = save.game_directory != NULL ? save.game_directory : "";
  for (size_t i = 0; i < catalog->product_count; i++) {
    const qa_catalog_discovered_product *candidate = &catalog->products[i];
    if (candidate->expectation.family != QA_GAME_FAMILY_Q2) continue;
    if (strcmp(candidate->expectation.edition, "rerelease") != 0) continue;
    const char *base = strrchr(candidate->expectation.content_directory, '/');
    base = base != NULL ? base + 1 : candidate->expectation.content_directory;
    bool equal = strlen(base) == strlen(wanted);
    if (equal) for (size_t c = 0; base[c] != '\0'; c++)
      if (tolower((unsigned char)base[c]) != tolower((unsigned char)wanted[c])) equal = false;
    if (equal) { product = candidate; break; }
  }
  if (product == NULL) {
    static char message[512];
    snprintf(message, sizeof message, "Retail Quake II save game directory %s is not installed.",
        save.game_directory != NULL ? save.game_directory : "(none)");
    *error_out = message;
    return false;
  }
  const char *map = save.map_command;
  if (map[0] == '*') map++;
  if (strncmp(map, "maps/", 5) == 0) map += 5;
  size_t map_len = strlen(map);
  if (map_len >= 4 && ends_with_ci(map, ".bsp")) map_len -= 4;
  char *map_name = malloc(map_len + 1);
  if (map_name == NULL) qa_range_error("Out of memory");
  memcpy(map_name, map, map_len); map_name[map_len] = '\0';
  bool valid = map_len > 0;
  for (size_t i = 0; valid && i < map_len; i++) {
    char ch = map_name[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' || ch == '-')) valid = false;
  }
  for (const char *p = map_name;;) {
    const char *slash = strchr(p, '/');
    size_t seg = slash != NULL ? (size_t)(slash - p) : strlen(p);
    if (seg == 0 || (seg == 1 && p[0] == '.') || (seg == 2 && p[0] == '.' && p[1] == '.')) valid = false;
    if (slash == NULL) break;
    p = slash + 1;
  }
  if (!valid) { free(map_name); *error_out = "Retail Quake II save has an invalid map name."; return false; }
  const char *skill_text = save.cvar(save.cvar_ctx, "skill");
  double skill_value = skill_text != NULL ? strtod(skill_text, NULL) : 1;
  double skill = skill_value == 0 || skill_value == 1 || skill_value == 2 || skill_value == 3 ? skill_value : 1;
  const char *mode = is(save.cvar(save.cvar_ctx, "deathmatch"), "1") ? "deathmatch"
      : is(save.cvar(save.cvar_ctx, "coop"), "1") ? "coop" : "singleplayer";
  qa_app_options restored = clone_options(load_options);
  strip_consumed(&restored);
  set_owned(&restored.product, product->expectation.id);
  char *map_path = malloc(map_len + 16);
  if (map_path == NULL) qa_range_error("Out of memory");
  snprintf(map_path, map_len + 16, "maps/%s.bsp", map_name);
  free(map_name);
  free(restored.map); restored.map = map_path;
  restored.skill = skill; restored.mode = mode;
  restored.movement = QA_GAME_FAMILY_Q2; restored.character = QA_GAME_FAMILY_Q2;
  set_owned(&restored.character_model, "male");
  restored.seats = 1; restored.rules = "standard";
  restored.network_kind = "offline";
  error = host->import_save(host->ctx, &restored, catalog, NULL, false, &out->image);
  if (error != NULL) { qa_app_options_destroy(&restored); *error_out = error; return false; }
  out->options = restored;
  return true;
}
bool qa_app_prepare_application_save(const qa_app_options *options, const qa_content_catalog *catalog,
    const char *path, const char *source_product, const qa_app_original_save_host *host,
    qa_app_prepared_application_save *out, const char **error_out) {
  memset(out, 0, sizeof *out);
  *error_out = NULL;
  qa_app_options load_options = clone_options(options);
  load_options.authored_campaign_start = false;
  bool result;
  if (ends_with_ci(path, ".pak.gz")) {
    result = prepare_kex(catalog, path, &load_options, host, out, error_out);
    qa_app_options_destroy(&load_options);
    return result;
  }
  qa_app_saved_game_view saved = {0};
  const char *error = host->read_saved_game(host->ctx, path, &saved);
  if (error != NULL) { qa_app_options_destroy(&load_options); *error_out = error; return false; }
  if (is(saved.kind, "shared")) {
    out->image = *saved.image;
    out->options = load_options;
    return true;
  }
  const qa_catalog_discovered_product *product =
      host->select_q1_save_product(host->ctx, catalog, saved.data, path, source_product);
  double skill = saved.skill;
  if (skill != 0 && skill != 1 && skill != 2 && skill != 3) {
    qa_app_options_destroy(&load_options);
    *error_out = "Original save skill must be 0..3";
    return false;
  }
  qa_app_options restored = clone_options(&load_options);
  qa_app_options_destroy(&load_options);
  strip_consumed(&restored);
  set_owned(&restored.product, product->expectation.id);
  char *map_path = malloc(strlen(saved.map) + 16);
  if (map_path == NULL) qa_range_error("Out of memory");
  snprintf(map_path, strlen(saved.map) + 16, "maps/%s.bsp", saved.map);
  free(restored.map); restored.map = map_path;
  restored.skill = skill; restored.mode = "singleplayer";
  restored.movement = QA_GAME_FAMILY_Q1; restored.character = QA_GAME_FAMILY_Q1;
  set_owned(&restored.character_model, "player");
  restored.seats = 1; restored.rules = "standard";
  set_owned(&restored.quakec_program, "progs.dat");
  restored.network_kind = "offline";
  error = host->import_save(host->ctx, &restored, catalog, &saved, true, &out->image);
  if (error != NULL) { qa_app_options_destroy(&restored); *error_out = error; return false; }
  out->options = restored;
  return true;
}
