/* Port of src/app/bootstrap/original-save.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/original_save.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/simulation/quakec_source.h"
#include "qa/app/bootstrap/simulation/shared_simulation.h"
#include "qa/contracts/identity.h"
#include "qa/foundation/js.h"
#include "qa/foundation/js_case.h"
#include "qa/foundation/support.h"
#include "qa/persistence/kex_q2_save.h"
#include "qa/compat/qc/q1_save.h"
#include "qa/persistence/q1_selection.h"
#include "qa/persistence/saved_game.h"
#include "simulation/shared_simulation_internal.h"

/* What keeps the prepared image alive (TS GC): the decoded shared save, or the closed content and simulation whose
 * recipe the checkpoint borrows. */
typedef struct save_backing {
  bool has_saved; qa_saved_game saved;
  qa_app_loaded_content *content;
  bool has_identity; qa_identity_owner identity;
  qa_app_shared_simulation *simulation;
  bool has_checkpoint; qa_app_shared_sim_save checkpoint;
} save_backing;

static void *checked(void *pointer) {
  if (pointer == NULL) qa_range_error("Out of memory");
  return pointer;
}
static char *dup_text(const char *text) {
  if (text == NULL) return NULL;
  size_t len = strlen(text);
  char *out = checked(malloc(len + 1));
  memcpy(out, text, len + 1);
  return out;
}
static void set_owned(char **slot, const char *text) { free(*slot); *slot = dup_text(text); }
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }

/* Deep copy of the owned fields of qa_app_options (TS object spread). */
static qa_app_options clone_options(const qa_app_options *source) {
  qa_app_options o = *source;
  o.network_transport_relay = dup_text(source->network_transport_relay);
  o.startup_commands = NULL; o.startup_command_count = 0;
  if (source->startup_command_count > 0) {
    o.startup_commands = checked(calloc(source->startup_command_count, sizeof *o.startup_commands));
    for (size_t i = 0; i < source->startup_command_count; i++) o.startup_commands[i] = dup_text(source->startup_commands[i]);
    o.startup_command_count = source->startup_command_count;
  }
  o.remote_content.directory = dup_text(source->remote_content.directory);
  o.server_profile_path = dup_text(source->server_profile_path);
  o.corpus_root = dup_text(source->corpus_root);
  o.user_content_root = dup_text(source->user_content_root);
  o.addon_content_roots = NULL; o.addon_content_root_count = 0;
  if (source->addon_content_root_count > 0) {
    o.addon_content_roots = checked(calloc(source->addon_content_root_count, sizeof *o.addon_content_roots));
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
    o.mods = checked(calloc(source->mod_count, sizeof *o.mods));
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
/* TS rest destructuring: drop movementProduct, mapProduct, q2GameLibrary, weaponBehavior, teamArenaSkirmish,
 * q3MapLaunch, q3Product, botSkill, serverProfile, serverProfilePath, startupCommands, explicitRules. (The C
 * explicitRules is the three explicit_* flags the parser records.) */
static void strip_source_options(qa_app_options *o) {
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
/* TS network: { kind: "offline" }: the network fields of the other kinds go with it. */
static void set_offline(qa_app_options *o) {
  o->network_kind = "offline";
  free(o->network_host); o->network_host = NULL;
  free(o->network_remote); o->network_remote = NULL;
  o->network_port = 0;
}

static void backing_free(save_backing *backing) {
  if (backing == NULL) return;
  if (backing->has_checkpoint) qa_app_shared_sim_save_free(&backing->checkpoint);
  if (backing->simulation != NULL) qa_app_shared_simulation_destroy(backing->simulation);
  if (backing->has_identity) qa_identity_owner_destroy(&backing->identity);
  if (backing->content != NULL) qa_app_loaded_content_destroy(backing->content);
  if (backing->has_saved) qa_saved_game_free(&backing->saved);
  free(backing);
}
static void backing_cleanup(void *backing) { backing_free(backing); }
static void options_cleanup(void *options) { qa_app_options_destroy(options); }
/* A raise inside the try closes the simulation, then the content (TS finally blocks, innermost first). */
static void simulation_close_cleanup(void *backing) {
  save_backing *b = backing;
  if (b->simulation != NULL) qa_app_shared_sim_close(b->simulation);
}
static void content_close_cleanup(void *backing) {
  save_backing *b = backing;
  if (b->content != NULL) qa_app_loaded_content_close(b->content);
}

/* Bun.file(path).bytes() rejection: the Node errno message. */
static void raise_read_error(int error, const char *path) {
  if (error == ENOENT) qa_raise("Error", 0, 0, "ENOENT: no such file or directory, open '%s'", path);
  if (error == EACCES) qa_raise("Error", 0, 0, "EACCES: permission denied, open '%s'", path);
  if (error == EISDIR) qa_raise("Error", 0, 0, "EISDIR: illegal operation on a directory, read");
  qa_raise("Error", 0, 0, "%s, open '%s'", error != 0 ? strerror(error) : "read failed", path);
}

/* The simulation of both import paths: createSimulation, admitPlayer(identity.client(0, 0)), the original restore
 * (save NULL for KEX), checkpoint. TS closes the simulation and then the content in finally blocks. */
static bool kex_player_identity(void *ctx, qa_client_id client, double *out_seat, const char **out_social_id) {
  (void)ctx; (void)client; /* TS playerIdentity: () => ({ seat: 0, socialId: "" }) */
  *out_seat = 0;
  *out_social_id = "";
  return true;
}
static void import_world(save_backing *backing, qa_app_options *restored, const qa_content_catalog *catalog,
    const char *identity_name, const qa_q1_save_data *original, qa_app_prepared_application_save *out) {
  /* loadApplicationContent(restored, undefined, undefined, catalog): the loader takes over its options. */
  backing->content = qa_app_load_application_content_native((qa_app_content_host){0}, clone_options(restored), NULL, false,
      NULL, (qa_content_catalog *)catalog, NULL);
  if (backing->content == NULL) qa_range_error("Out of memory");
  qa_cleanup close_content;
  qa_cleanup_push(&close_content, content_close_cleanup, backing);
  const struct qa_app_sim_prepared_quakec_source *prepared = qa_app_loaded_content_prepared_quakec(backing->content);
  if (original != NULL && prepared == NULL) qa_raise("Error", 0, 0, "Original save requires its native QuakeC program");
  if (!qa_identity_owner_create(&backing->identity, identity_name)) qa_range_error("Out of memory");
  backing->has_identity = true;
  qa_app_application_world world = qa_app_loaded_content_world(backing->content);
  qa_app_simulation_options options;
  memset(&options, 0, sizeof options);
  options.identity = backing->identity;
  options.recipe = *qa_app_loaded_content_recipe(backing->content);
  options.world = &world;
  options.mounts = qa_app_loaded_content_mounts(backing->content);
  if (original != NULL) {
    options.prepared_quakec = prepared;
    options.original_save_candidate = true;
    options.has_initial_source_milliseconds = true;
    options.initial_source_milliseconds = original->time * 1000;
  } else {
    options.identity_ctx = backing;
    options.player_identity = kex_player_identity;
  }
  options.dedicated = true;
  options.skill = restored->skill;
  options.mode = restored->mode;
  options.seed = restored->seed;
  options.max_clients = 1;
  backing->simulation = qa_app_shared_simulation_create(options);
  qa_cleanup close_simulation;
  qa_cleanup_push(&close_simulation, simulation_close_cleanup, backing);
  (void)qa_app_shared_sim_admit_player(backing->simulation, qa_identity_owner_client(&backing->identity, 0, 0));
  if (original != NULL) {
    qa_q1_unknown_save_fields unknown;
    memset(&unknown, 0, sizeof unknown);
    qa_app_shared_sim_restore_original_save(backing->simulation, original, &unknown);
    qa_q1_unknown_save_fields_free(&unknown); /* TS discards restoreQ1SourceSave's result */
  }
  qa_app_shared_sim_checkpoint(backing->simulation, &backing->checkpoint);
  backing->has_checkpoint = true;
  qa_cleanup_pop(&close_simulation);
  qa_app_shared_sim_close(backing->simulation);
  qa_cleanup_pop(&close_content);
  qa_app_loaded_content_close(backing->content);
  out->image = backing->checkpoint.image;
}

/* TS prepareKexQ2Save: retail KEX transition saves synthesize a fresh-spawn image on the recorded map with the
 * recorded server cvars; saves carrying client or level state are refused. */
static const char *kex_cvar(const qa_kex_q2_save_archive *save, const char *name) {
  const char *value = NULL; /* Map semantics: a later duplicate wins */
  for (size_t i = 0; i < save->info.cvar_count; i++)
    if (is(save->info.cvars[i].name, name)) value = save->info.cvars[i].value;
  return value;
}
static void archive_cleanup(void *archive) { qa_kex_q2_save_archive_free(archive); }
static void prepare_kex(const qa_content_catalog *catalog, const char *path, const qa_app_options *load_options,
    save_backing *backing, qa_app_prepared_application_save *out) {
  qa_kex_q2_save_archive save;
  memset(&save, 0, sizeof save);
  if (!qa_read_kex_q2_save_archive(path, &save)) qa_range_error("Out of memory");
  qa_cleanup save_cleanup;
  qa_cleanup_push(&save_cleanup, archive_cleanup, &save);
  if (!qa_kex_q2_save_restorable(&save))
    qa_raise("Error", 0, 0, "Retail Quake II saves with recorded client or level state cannot be restored by this engine yet.");
  const qa_catalog_discovered_product *product = NULL;
  const char *wanted = save.info.game_directory != NULL ? save.info.game_directory : "";
  for (size_t i = 0; i < catalog->product_count && product == NULL; i++) {
    const qa_catalog_discovered_product *candidate = &catalog->products[i];
    if (candidate->expectation.family != QA_GAME_FAMILY_Q2 || !is(candidate->expectation.edition, "rerelease")) continue;
    const char *base = strrchr(candidate->expectation.content_directory, '/');
    base = base != NULL ? base + 1 : candidate->expectation.content_directory;
    if (qa_js_equals_ignore_case(base, wanted)) product = candidate; /* basename(...).toLowerCase() === ...toLowerCase() */
  }
  if (product == NULL)
    qa_raise("Error", 0, 0, "Retail Quake II save game directory %s is not installed.",
        save.info.game_directory != NULL ? save.info.game_directory : "(none)");
  /* mapCommand.replace(/^\*\/, "").replace(/^maps\//, "").replace(/\.bsp$/i, "") */
  const char *map = save.info.map_command != NULL ? save.info.map_command : "";
  if (map[0] == '*') map++;
  if (strncmp(map, "maps/", 5) == 0) map += 5;
  size_t map_len = strlen(map);
  if (qa_js_ends_with_ignore_case(map, ".bsp")) map_len -= 4;
  char *map_name = checked(malloc(map_len + 1));
  memcpy(map_name, map, map_len);
  map_name[map_len] = '\0';
  qa_cleanup map_cleanup;
  qa_cleanup_push(&map_cleanup, free, map_name);
  bool valid = map_len > 0; /* /^[A-Za-z0-9_/-]+$/ */
  for (size_t i = 0; valid && i < map_len; i++) {
    char ch = map_name[i];
    if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' || ch == '-'))
      valid = false;
  }
  for (const char *p = map_name; valid;) { /* split("/").some(part => "" | "." | "..") */
    const char *slash = strchr(p, '/');
    size_t segment = slash != NULL ? (size_t)(slash - p) : strlen(p);
    if (segment == 0 || (segment == 1 && p[0] == '.') || (segment == 2 && p[0] == '.' && p[1] == '.')) valid = false;
    if (slash == NULL) break;
    p = slash + 1;
  }
  if (!valid) qa_raise("Error", 0, 0, "Retail Quake II save has an invalid map name.");
  const char *skill_text = kex_cvar(&save, "skill");
  double skill_value = qa_js_string_to_number(skill_text != NULL ? skill_text : "1"); /* Number(get("skill") ?? "1") */
  double skill = skill_value == 0 || skill_value == 1 || skill_value == 2 || skill_value == 3 ? skill_value : 1;
  const char *mode = is(kex_cvar(&save, "deathmatch"), "1") ? "deathmatch" : is(kex_cvar(&save, "coop"), "1") ? "coop" : "singleplayer";
  qa_app_options restored = clone_options(load_options);
  qa_cleanup restored_cleanup;
  qa_cleanup_push(&restored_cleanup, options_cleanup, &restored);
  strip_source_options(&restored);
  set_owned(&restored.product, product->expectation.id);
  size_t map_path_length = map_len + 10;
  char *map_path = checked(malloc(map_path_length));
  snprintf(map_path, map_path_length, "maps/%s.bsp", map_name);
  free(restored.map);
  restored.map = map_path;
  restored.skill = skill;
  restored.mode = mode;
  restored.movement = QA_GAME_FAMILY_Q2;
  restored.character = QA_GAME_FAMILY_Q2;
  set_owned(&restored.character_model, "male");
  restored.seats = 1;
  restored.rules = "standard";
  set_offline(&restored);
  import_world(backing, &restored, catalog, "kex-q2-save-import", NULL, out);
  qa_cleanup_pop(&restored_cleanup);
  out->options = restored;
  qa_cleanup_pop(&map_cleanup);
  free(map_name);
  qa_cleanup_pop(&save_cleanup);
  qa_kex_q2_save_archive_free(&save);
}

void qa_app_prepare_application_save_native(const qa_app_options *options, const qa_content_catalog *catalog, const char *path,
    const char *source_product, qa_app_prepared_application_save *out, void **out_backing) {
  memset(out, 0, sizeof *out);
  *out_backing = NULL;
  save_backing *backing = checked(calloc(1, sizeof *backing));
  qa_cleanup backing_node;
  qa_cleanup_push(&backing_node, backing_cleanup, backing);
  /* const { authoredCampaignStart, ...loadOptions } = options */
  qa_app_options load_options = clone_options(options);
  load_options.authored_campaign_start = false;
  qa_cleanup load_cleanup;
  qa_cleanup_push(&load_cleanup, options_cleanup, &load_options);
  if (qa_js_ends_with_ignore_case(path, ".pak.gz")) { /* path.toLowerCase().endsWith(".pak.gz") */
    prepare_kex(catalog, path, &load_options, backing, out);
    qa_cleanup_pop(&load_cleanup);
    qa_app_options_destroy(&load_options);
  } else {
    errno = 0;
    if (!qa_read_saved_game(path, &backing->saved)) raise_read_error(errno, path);
    backing->has_saved = true;
    if (backing->saved.kind == QA_SAVED_GAME_SHARED) {
      out->image = backing->saved.image;
      qa_cleanup_pop(&load_cleanup);
      out->options = load_options;
    } else {
      const qa_q1_save_data *data = &backing->saved.data;
      const qa_catalog_discovered_product *product = qa_select_q1_save_product_in_catalog(catalog, data, path, source_product);
      double skill = data->skill;
      if (skill != 0 && skill != 1 && skill != 2 && skill != 3) qa_raise("Error", 0, 0, "Original save skill must be 0..3");
      qa_app_options restored = clone_options(&load_options);
      qa_cleanup restored_cleanup;
      qa_cleanup_push(&restored_cleanup, options_cleanup, &restored);
      strip_source_options(&restored);
      set_owned(&restored.product, product->expectation.id);
      const char *map = data->map != NULL ? data->map : "";
      size_t map_path_length = strlen(map) + 10;
      char *map_path = checked(malloc(map_path_length));
      snprintf(map_path, map_path_length, "maps/%s.bsp", map);
      free(restored.map);
      restored.map = map_path;
      restored.skill = skill;
      restored.mode = "singleplayer";
      restored.movement = QA_GAME_FAMILY_Q1;
      restored.character = QA_GAME_FAMILY_Q1;
      set_owned(&restored.character_model, "player");
      restored.seats = 1;
      restored.rules = "standard";
      set_owned(&restored.quakec_program, "progs.dat");
      set_offline(&restored);
      import_world(backing, &restored, catalog, "original-save-import", data, out);
      qa_cleanup_pop(&restored_cleanup);
      out->options = restored;
      qa_cleanup_pop(&load_cleanup);
      qa_app_options_destroy(&load_options);
    }
  }
  qa_cleanup_pop(&backing_node);
  *out_backing = backing;
}

void qa_app_prepared_application_save_free(qa_app_prepared_application_save *save, void *backing) {
  qa_app_options_destroy(&save->options);
  backing_free(backing);
  memset(save, 0, sizeof *save);
}
