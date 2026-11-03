/* Port of src/app/bootstrap/configuration.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/configuration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/gtv_commands.h"
#include "qa/app/bootstrap/input_devices.h"
#include "qa/app/bootstrap/player_userinfo.h"
#include "qa/app/bootstrap/q1_client_commands.h"
#include "qa/app/bootstrap/q2_client_commands.h"
#include "qa/app/bootstrap/q3_client/userinfo.h"
#include "qa/app/bootstrap/q3_map_command.h"
#include "qa/app/bootstrap/shared_setting_cvars.h"
#include "qa/app/bootstrap/startup_commands.h"
#include "qa/app/bootstrap/team_arena_skirmish.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/core/commands/text.h"
#include "qa/content/catalog/source_program.h"
#include "qa/content/mounts.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/settings/server/selection.h"
#include "qa/foundation/js.h"
qa_command_dialect qa_app_configuration_dialect_for(qa_game_family family, const char *edition) {
  if (family == QA_GAME_FAMILY_Q1) return edition && strcmp(edition, "quakeworld") == 0 ? QA_COMMAND_DIALECT_Q1_QUAKEWORLD : QA_COMMAND_DIALECT_Q1_NETQUAKE;
  if (family == QA_GAME_FAMILY_Q2) return edition && strcmp(edition, "rerelease") == 0 ? QA_COMMAND_DIALECT_Q2_RERELEASE : QA_COMMAND_DIALECT_Q2_CLASSIC;
  return QA_COMMAND_DIALECT_Q3;
}
qa_command_dialect qa_app_configuration_dialect(qa_app_configuration_host host, const qa_app_configuration_content *content) {
  qa_app_application_configuration_selection selection = qa_app_configuration_content_selection(content);
  qa_game_family family = QA_GAME_FAMILY_Q1;
  const char *edition = NULL, *directory = NULL, *user_root = NULL, *base = NULL;
  host.product(host.ctx, selection.engine_behavior.content, &family, &edition, &directory, &user_root, &base);
  return qa_app_configuration_dialect_for(family, edition);
}
bool qa_app_configuration_store(qa_app_configuration_host host, const qa_app_options *options,
                                const qa_app_configuration_content *content, const char *reference, qa_config_store *out) {
  (void)content;
  qa_game_family family = QA_GAME_FAMILY_Q1;
  const char *edition = NULL, *directory = NULL, *user_root = NULL, *base = NULL;
  host.product(host.ctx, reference, &family, &edition, &directory, &user_root, &base);
  if (user_root) return qa_config_store_init(out, user_root);
  char *fallback = options->user_content_root ? NULL : qa_default_user_content_root();
  char *path = qa_user_product_directory(options->user_content_root ? options->user_content_root : fallback, directory);
  bool ok = qa_config_store_init(out, path);
  free(path);
  free(fallback);
  return ok;
}
bool qa_app_legacy_configuration_options(qa_app_configuration_host host, const qa_app_options *options, const char *reference,
                                         qa_app_legacy_console_config_sources *out, const char **roots_storage, size_t capacity, char **owned_storage) {
  qa_game_family family = QA_GAME_FAMILY_Q1;
  const char *edition = NULL, *directory = NULL, *user_root = NULL, *base = NULL;
  host.product(host.ctx, reference, &family, &edition, &directory, &user_root, &base);
  if (family != QA_GAME_FAMILY_Q1 || (edition && strcmp(edition, "quakeworld") == 0)) return false;
  char *fallback = options->user_content_root ? NULL : qa_default_user_content_root();
  const char *shared_root = options->user_content_root ? options->user_content_root : fallback;
  owned_storage[0] = fallback;
  const char *directories[64];
  const char *roots[64];
  size_t count = host.q1_products(host.ctx, directories, roots, 64);
  size_t written = 0;
  /* Selected product first, then every q1 non-quakeworld product. */
  const char *selected_root = user_root;
  char *selected_owned = NULL;
  if (!selected_root) selected_owned = qa_user_product_directory(shared_root, directory);
  roots_storage[written++] = selected_root ? selected_root : selected_owned;
  owned_storage[1] = selected_owned;
  for (size_t i = 0; i < count && written < capacity; i++) {
    const char *root = roots[i];
    char *owned = NULL;
    if (!root) owned = qa_user_product_directory(shared_root, directories[i]);
    const char *value = root ? root : owned;
    bool seen = false;
    for (size_t j = 0; j < written; j++) if (strcmp(roots_storage[j], value) == 0) seen = true;
    if (seen) { free(owned); continue; }
    roots_storage[written] = value;
    owned_storage[1 + written] = owned;
    written++;
  }
  out->shared_root = shared_root;
  out->game_roots = roots_storage;
  out->game_root_count = written;
  return true;
}
static const char *const routed_commands[] = {
    "save", "load", "weapnext", "weapprev", "use", "weapon", "say", "say_team", "connect", "disconnect", "quit",
    "mvdconnect", "mvdisconnect", "in_restart", "midiinfo", "local_join", "local_drop", "downloadstatus", "stopdownload",
    "retrydownload", "demopause", "cinematic", "cinematicpause", "stopcinematic", "record", "rerecord", "stop",
    "stoprecord", "mvdrecord", "mvdstop", "serverrecord", "serverstop", "playdemo", "demo", "demomap", "startdemos",
    "demos", "stopdemo"};
const char *const *qa_app_configuration_routed_commands(size_t *out_count) {
  *out_count = sizeof routed_commands / sizeof routed_commands[0];
  return routed_commands;
}
qa_app_configuration_content *qa_app_open_initial_configuration_content(qa_app_configuration_host host, qa_app_content_host content_host,
                                                                        qa_app_options options, const qa_executable_recipe *recipe, bool has_recipe) {
  (void)options; (void)recipe; (void)has_recipe;
  qa_app_configuration_request request = {has_recipe ? "recipe" : "launch", NULL, {0}};
  qa_app_configuration_content *content = qa_app_open_application_configuration_content(content_host, request);
  if (content && host.open_initial) {
    const char *error = host.open_initial(host.ctx, content);
    if (error) qa_range_error("%s", error);
  }
  return content;
}
const char *qa_app_configuration_dialect_name(qa_command_dialect dialect) {
  switch (dialect) {
    case QA_COMMAND_DIALECT_Q1_NETQUAKE: return "q1-netquake";
    case QA_COMMAND_DIALECT_Q1_QUAKEWORLD: return "q1-quakeworld";
    case QA_COMMAND_DIALECT_Q2_CLASSIC: return "q2-classic";
    case QA_COMMAND_DIALECT_Q2_RERELEASE: return "q2-rerelease";
    case QA_COMMAND_DIALECT_Q3: return "q3";
  }
  return "q3";
}
qa_command_dialect qa_app_configuration_dialect_for_product(const qa_catalog_discovered_product *product) {
  return qa_app_configuration_dialect_for(product->expectation.family, product->expectation.edition);
}
qa_command_dialect qa_app_configuration_dialect_in_catalog(const qa_content_catalog *catalog,
                                                           const qa_app_configuration_content *content) {
  qa_app_application_configuration_selection selection = qa_app_configuration_content_selection(content);
  return qa_app_configuration_dialect_for_product(qa_content_catalog_product(catalog, selection.engine_behavior.content));
}
static qa_command_dialect dialect_for_clock(qa_clock_profile_kind kind) {
  switch (kind) {
    case QA_CLOCK_Q1_NETQUAKE: return QA_COMMAND_DIALECT_Q1_NETQUAKE;
    case QA_CLOCK_Q1_QUAKEWORLD: return QA_COMMAND_DIALECT_Q1_QUAKEWORLD;
    case QA_CLOCK_Q2_CLASSIC: return QA_COMMAND_DIALECT_Q2_CLASSIC;
    case QA_CLOCK_Q2_RERELEASE: return QA_COMMAND_DIALECT_Q2_RERELEASE;
    case QA_CLOCK_Q3: return QA_COMMAND_DIALECT_Q3;
  }
  return QA_COMMAND_DIALECT_Q3;
}
qa_command_dialect qa_app_configuration_movement_dialect_for(const qa_provider_timing *timing, size_t timing_count,
                                                             qa_provider_id movement_provider) {
  for (size_t i = 0; i < timing_count; i++)
    if (timing[i].provider && movement_provider && strcmp(timing[i].provider, movement_provider) == 0)
      return dialect_for_clock(timing[i].clock.kind);
  qa_range_error("Configuration has no timing for %s", movement_provider ? movement_provider : "");
}
qa_command_dialect qa_app_configuration_movement_dialect(const qa_app_application_configuration_selection *selection) {
  return qa_app_configuration_movement_dialect_for(&selection->timing, 1, selection->movement.provider);
}
bool qa_app_configuration_store_for_catalog(const qa_content_catalog *catalog, const qa_app_options *options,
                                            const char *reference, qa_config_store *out) {
  const qa_catalog_discovered_product *product = qa_content_catalog_product(catalog, reference);
  if (product->user_content && product->user_content->root) return qa_config_store_init(out, product->user_content->root);
  char *fallback = options->user_content_root ? NULL : qa_default_user_content_root();
  char *path = qa_user_product_directory(options->user_content_root ? options->user_content_root : fallback,
                                         product->expectation.content_directory);
  bool ok = path && qa_config_store_init(out, path);
  free(path);
  free(fallback);
  return ok;
}
bool qa_app_legacy_configuration_options_for_catalog(const qa_content_catalog *catalog, const qa_app_options *options,
                                                     const char *reference, qa_app_legacy_console_config_sources *out,
                                                     const char **roots_storage, size_t capacity, char **owned_storage) {
  const qa_catalog_discovered_product *selected = qa_content_catalog_product(catalog, reference);
  if (selected->expectation.family != QA_GAME_FAMILY_Q1 ||
      (selected->expectation.edition && strcmp(selected->expectation.edition, "quakeworld") == 0)) return false;
  if (capacity == 0) return false;
  char *fallback = options->user_content_root ? NULL : qa_default_user_content_root();
  const char *shared_root = options->user_content_root ? options->user_content_root : fallback;
  owned_storage[0] = fallback;
  size_t written = 0;
  /* TS: [selected, ...every q1 non-quakeworld product], deduped. */
  for (size_t pass = 0; pass < 2 && written < capacity; pass++) {
    for (size_t i = 0; i < (pass == 0 ? 1 : catalog->product_count) && written < capacity; i++) {
      const qa_catalog_discovered_product *product = pass == 0 ? selected : &catalog->products[i];
      if (pass == 1 && (product->expectation.family != QA_GAME_FAMILY_Q1 ||
          (product->expectation.edition && strcmp(product->expectation.edition, "quakeworld") == 0))) continue;
      const char *root = product->user_content ? product->user_content->root : NULL;
      char *owned = root ? NULL : qa_user_product_directory(shared_root, product->expectation.content_directory);
      const char *value = root ? root : owned;
      bool seen = false;
      for (size_t j = 0; j < written; j++) if (value && roots_storage[j] && strcmp(roots_storage[j], value) == 0) seen = true;
      if (seen) { free(owned); continue; }
      roots_storage[written] = value;
      owned_storage[1 + written] = owned;
      written++;
    }
  }
  out->shared_root = shared_root;
  out->game_roots = roots_storage;
  out->game_root_count = written;
  return true;
}
/* ---- TS sourceConsoleScripts ---- */
typedef struct source_scripts {
  qa_app_main_mount_lease lease; bool released;
  qa_content_mounted *mounts;
  qa_app_source_script_reader reader;
  const char **game_roots; char **owned_roots; size_t owned_count;
  char *console_root;
} source_scripts;
static bool source_scripts_open(void *ctx, const char *name, uint8_t **out_bytes, size_t *out_count) {
  source_scripts *scripts = ctx;
  qa_content_opened_resource opened;
  if (!qa_content_mounted_open(scripts->mounts, name, NULL, NULL, &opened)) return false;
  *out_bytes = opened.bytes; *out_count = opened.byte_length;
  return true;
}
static char **source_scripts_files(void *ctx, const char *directory, const char *extension, size_t *out_count) {
  source_scripts *scripts = ctx;
  char **names = qa_content_mounted_list_files(scripts->mounts, directory, extension, out_count);
  if (names == NULL) qa_range_error("Out of memory");
  return names;
}
static const char *source_scripts_retire(void *ctx) {
  source_scripts *scripts = ctx;
  if (!scripts->released) { scripts->released = true; scripts->lease.release(scripts->lease.ctx); }
  return NULL;
}
static void source_scripts_release(void *ctx) {
  source_scripts *scripts = ctx;
  /* Destroyed without close: nothing can read the mounts any more, so the lease ends with the files. */
  (void)source_scripts_retire(scripts);
  qa_app_source_script_reader_destroy(scripts->reader);
  for (size_t i = 0; i < scripts->owned_count; i++) free(scripts->owned_roots[i]);
  free(scripts->owned_roots); free(scripts->game_roots); free(scripts->console_root);
  free(scripts);
}
qa_app_console_script_files *qa_app_source_console_scripts(qa_app_loaded_content *content, const qa_app_options *options,
    const qa_config_store *settings) {
  const qa_content_catalog *catalog = qa_app_loaded_content_catalog(content);
  const char *source = qa_app_loaded_content_recipe(content)->engine_behavior.content;
  source_scripts *scripts = calloc(1, sizeof *scripts);
  if (scripts == NULL) qa_range_error("Out of memory");
  scripts->mounts = (qa_content_mounted *)qa_app_loaded_content_mounts(content);
  scripts->lease = qa_app_loaded_content_retain_main_mounts(content);
  qa_app_console_script_files_options files;
  memset(&files, 0, sizeof files);
  size_t capacity = catalog->product_count + 1;
  scripts->game_roots = calloc(capacity, sizeof *scripts->game_roots);
  scripts->owned_roots = calloc(capacity + 1, sizeof *scripts->owned_roots);
  scripts->owned_count = capacity + 1;
  scripts->console_root = qa_app_console_config_root(options->user_content_root);
  if (scripts->game_roots == NULL || scripts->owned_roots == NULL || scripts->console_root == NULL) qa_range_error("Out of memory");
  files.has_legacy_config = qa_app_legacy_configuration_options_for_catalog(catalog, options, source, &files.legacy_config,
      scripts->game_roots, capacity, scripts->owned_roots);
  files.console_root = scripts->console_root;
  files.settings = settings;
  scripts->reader = qa_app_source_script_reader_create(catalog, scripts->mounts, source);
  files.mounted_script = scripts->reader; files.has_mounted_script = true;
  files.mounted_ctx = scripts;
  files.mounted_resource = source_scripts_open;
  files.mounted_files = source_scripts_files;
  files.mounted.ctx = scripts; files.mounted.open = source_scripts_open; files.has_mounted = true;
  files.retire_mounted = source_scripts_retire;
  files.release = source_scripts_release; files.release_ctx = scripts;
  qa_app_console_script_files *result = qa_app_console_script_files_create(files);
  if (result == NULL) qa_range_error("Out of memory");
  return result;
}
static size_t script_roots_of(const qa_catalog_discovered_product *product, const char **roots) {
  size_t count = 0;
  if (product->user_content && product->user_content->root) roots[count++] = product->user_content->root;
  if (product->loose_root) roots[count++] = product->loose_root;
  return count;
}
qa_app_configuration_script_roots qa_app_configuration_script_roots_for(const qa_content_catalog *catalog,
                                                                        const qa_app_application_configuration_selection *selection) {
  qa_app_configuration_script_roots out = {0};
  out.product = qa_content_catalog_product(catalog, selection->engine_behavior.content);
  out.base = out.product->expectation.base_product == NULL ? out.product
      : qa_content_catalog_product(catalog, out.product->expectation.base_product);
  out.base_root_count = script_roots_of(out.base, out.base_roots);
  out.game_root_count = script_roots_of(out.product, out.game_roots);
  out.has_mod = strcmp(out.product->expectation.content_directory, out.base->expectation.content_directory) != 0;
  return out;
}
char *qa_app_configuration_seat_profile_name(size_t seat_index) {
  char buffer[64];
  int length = snprintf(buffer, sizeof buffer, "input/seat-%zu.json", seat_index + 1);
  if (length < 0) return NULL;
  char *out = malloc((size_t)length + 1);
  if (out) memcpy(out, buffer, (size_t)length + 1);
  return out;
}
qa_cvar_archive_entry *qa_app_configuration_load_client_archive(const qa_config_store *store, const char *content,
                                                                const char *provider, size_t seat_index,
                                                                qa_command_dialect dialect, size_t *out_count) {
  char index[32];
  (void)snprintf(index, sizeof index, "%zu", seat_index);
  const char *owner[] = {"client", content, provider, index};
  return qa_app_load_cvar_archive(store, owner, 4, dialect, out_count);
}
qa_cvar_archive_entry *qa_app_configuration_load_input_archive(const qa_config_store *store, size_t seat_index,
                                                               qa_command_dialect dialect, size_t *out_count) {
  char index[32];
  (void)snprintf(index, sizeof index, "%zu", seat_index);
  const char *owner[] = {"input", qa_app_configuration_dialect_name(dialect), index};
  return qa_app_load_cvar_archive(store, owner, 3, dialect, out_count);
}
qa_cvar_archive_entry *qa_app_configuration_load_movement_archive(const qa_config_store *store,
                                                                  qa_command_dialect movement, size_t *out_count) {
  const char *owner[] = {"movement", qa_app_configuration_dialect_name(movement)};
  return qa_app_load_cvar_archive(store, owner, 2, movement, out_count);
}
qa_cvar_archive_entry *qa_app_configuration_load_fallback_archive(const qa_config_store *store,
                                                                  qa_command_dialect dialect, size_t *out_count) {
  const char *owner[] = {"fallback", qa_app_configuration_dialect_name(dialect)};
  return qa_app_load_cvar_archive(store, owner, 2, dialect, out_count);
}
qa_app_configuration_command_request qa_app_configuration_make_request(const char *name, const char *const *arguments,
                                                                       size_t argument_count, qa_command_context source) {
  const qa_command_origin *origin = &source.origin;
  while (origin->kind == QA_COMMAND_ORIGIN_SCRIPT && origin->caller) origin = origin->caller;
  qa_app_configuration_command_request request = {0};
  request.target = "application";
  request.name = name;
  request.arguments = arguments;
  request.argument_count = argument_count;
  request.has_seat = origin->kind == QA_COMMAND_ORIGIN_LOCAL_SEAT;
  if (request.has_seat) request.seat = origin->seat;
  request.source = source;
  return request;
}
qa_app_configuration_content *qa_app_open_initial_configuration_content_for_catalog(qa_app_content_host content_host,
    const qa_content_catalog *catalog, qa_app_options *options, const qa_executable_recipe *recipe, bool has_recipe,
    const qa_app_q3_product_host *q3_host) {
  const char *content_id = has_recipe && recipe ? recipe->map.entities.content : options->product;
  qa_app_prepared_q3_product prepared = qa_app_prepare_q3_application_product(catalog, content_id, options, q3_host);
  catalog = prepared.catalog;
  if (prepared.has_q3_product) {
    options->has_q3_product = true;
    options->q3_product = prepared.q3_product;
  }
  qa_app_configuration_content *content;
  if (has_recipe) {
    qa_app_configuration_request request = {"recipe", NULL, {0}};
    content = qa_app_open_application_configuration_content(content_host, request);
  } else {
    qa_launch_preset preset = qa_app_application_configuration_preset(content_host, options);
    qa_app_configuration_request request = {"launch", &preset, qa_preset_choice(preset.id)};
    content = qa_app_open_application_configuration_content(content_host, request);
  }
  if (content) {
    qa_app_configuration_content_set_catalog(content, catalog);
    if (options->has_q3_product) qa_app_configuration_content_set_q3_product(content, &options->q3_product);
  }
  return content;
}
/* --- configurationScriptReader --- */
static uint8_t *reader_mounted(void *ctx, const char *name, size_t *out_length) {
  uint8_t *bytes = NULL;
  size_t length = 0;
  if (!qa_app_console_script_files_read_mounted_script(ctx, name, &bytes, &length)) return NULL;
  *out_length = length;
  return bytes;
}
static char *reader_user(void *ctx, const char *name, qa_command_context source) {
  return qa_app_console_script_files_read(ctx, name, source);
}
void qa_app_configuration_script_reader(const qa_content_catalog *catalog,
    const qa_app_application_configuration_selection *selection, const qa_app_options *options,
    qa_app_console_script_files *scripts, qa_app_prepared_scoped_read_fn *out_read, void **out_ctx) {
  qa_app_configuration_script_roots roots = qa_app_configuration_script_roots_for(catalog, selection);
  /* Root arrays live with the reader, as the TS closure captures. */
  const char **base_roots = malloc((roots.base_root_count + 1) * sizeof *base_roots);
  const char **game_roots = malloc((roots.game_root_count + 1) * sizeof *game_roots);
  if (!base_roots || !game_roots) qa_range_error("Out of memory");
  memcpy(base_roots, roots.base_roots, roots.base_root_count * sizeof *base_roots);
  memcpy(game_roots, roots.game_roots, roots.game_root_count * sizeof *game_roots);
  qa_app_startup_script_reader_options reader = {0};
  reader.ctx = scripts;
  reader.mounted = reader_mounted;
  reader.user = reader_user;
  reader.base_loose_roots = base_roots;
  reader.base_loose_root_count = roots.base_root_count;
  reader.game_loose_roots = game_roots;
  reader.game_loose_root_count = roots.game_root_count;
  reader.seat_root = qa_app_console_config_root(options->user_content_root);
  qa_app_create_startup_script_reader(&reader, out_read, out_ctx);
}
void qa_app_configuration_script_reader_free(void *ctx) {
  qa_app_startup_script_reader_options *reader = ctx;
  if (reader == NULL) return;
  free((void *)reader->base_loose_roots);
  free((void *)reader->game_loose_roots);
  free((void *)reader->seat_root);
  free(reader);
}
/* --- prepareInitialConfiguration --- */
struct qa_app_prepared_initial {
  qa_app_prepared_startup *prepared;
  qa_app_image_settings *image;
  qa_app_options options;
  double max_clients;
  qa_app_configuration_command_request *requests;
  size_t request_count, request_capacity;
  qa_app_console_script_files *scripts;
  qa_session_seat **local_seats; qa_client_id *local_clients; size_t local_seat_count;
  /* Owned owners created here. */
  qa_cvar_registry *source, *input_cvars;
  qa_cvar_registry **seat_cvars, **mouse_cvars; size_t seat_owner_count;
  qa_seat_settings *profiles; bool *has_profile;
  qa_cvar_archive_entry **archives; size_t *archive_counts;
  qa_cvar_archive_entry **mouse_archives; size_t *mouse_archive_counts;
  qa_cvar_archive_entry *shared_archive; size_t shared_archive_count, shared_archive_capacity;
  qa_cvar_archive_entry *movement_archive; size_t movement_archive_count;
  qa_cvar_archive_entry *fallback_archive; size_t fallback_archive_count;
  char **shared_names; size_t shared_name_count;
  /* applyLaunchOptions closure state. */
  const qa_app_prepare_initial_args *args;
  qa_app_application_configuration_selection selection;
  qa_app_team_arena_launch_overrides *team_arena;
  bool resolved;
  void *read_ctx; /* configurationScriptReader's ctx (TS closure), freed with the initial configuration */
};
static char *initial_dup(const char *text) {
  char *copy = strdup(text);
  if (!copy) qa_range_error("Out of memory");
  return copy;
}
static void shared_push(qa_app_prepared_initial *initial, const char *name, const char *value) {
  if (initial->shared_archive_count == initial->shared_archive_capacity) {
    size_t capacity = initial->shared_archive_capacity == 0 ? 8 : initial->shared_archive_capacity * 2;
    qa_cvar_archive_entry *grown = realloc(initial->shared_archive, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    initial->shared_archive = grown;
    initial->shared_archive_capacity = capacity;
  }
  initial->shared_archive[initial->shared_archive_count].name = initial_dup(name);
  initial->shared_archive[initial->shared_archive_count].value = initial_dup(value);
  initial->shared_archive_count++;
}
static void shared_push_number(qa_app_prepared_initial *initial, const char *name, double value) {
  char *text = qa_input_js_number(value);
  if (!text) qa_range_error("Out of memory");
  shared_push(initial, name, text);
  free(text);
}
static void host_print_source(void *ctx, const char *text, const qa_command_context *source) {
  const qa_app_prepare_initial_args *args = ctx;
  (void)source;
  if (args->print) args->print(args->print_ctx, text);
}
static int cheats_allowed(void *ctx) {
  return qa_cvar_registry_variable_value(ctx, "sv_cheats") == 1 ? 1 : 0;
}
static void request_forward(void *ctx, const char *name, const char *const *args, size_t arg_count,
    qa_command_context source) {
  qa_app_prepared_initial *initial = ctx;
  qa_app_configuration_command_request request = qa_app_configuration_make_request(name, args, arg_count, source);
  if (initial->request_count == initial->request_capacity) {
    size_t capacity = initial->request_capacity == 0 ? 8 : initial->request_capacity * 2;
    qa_app_configuration_command_request *grown = realloc(initial->requests, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    initial->requests = grown;
    initial->request_capacity = capacity;
  }
  /* Deep-copy the borrowed dispatch strings. */
  request.name = initial_dup(name);
  const char **copied = NULL;
  if (arg_count > 0) {
    copied = malloc(arg_count * sizeof *copied);
    if (!copied) qa_range_error("Out of memory");
    for (size_t i = 0; i < arg_count; i++) copied[i] = initial_dup(args[i]);
  }
  request.arguments = copied;
  /* The dispatch context's origin chain is freed with its command: keep a copy (TS keeps the object). */
  request.source = qa_command_context_copy(&request.source);
  initial->requests[initial->request_count++] = request;
}
static void set_cvar_text(qa_cvar_registry *registry, const char *name, const char *value, bool force) {
  qa_cvar_snapshot snapshot = {0};
  if (qa_cvar_registry_set(registry, name, value, force, &snapshot)) qa_cvar_read_free(&snapshot);
}
static void view_changed(void *ctx, double value) {
  char *text = qa_input_js_number(value);
  if (!text) qa_range_error("Out of memory");
  set_cvar_text(ctx, "fov", text, false);
  free(text);
}
static void set_image_number(qa_app_prepared_initial *initial, const char *name, double value) {
  if (initial->image == NULL) return;
  char *text = qa_input_js_number(value);
  if (!text) qa_range_error("Out of memory");
  set_cvar_text(qa_app_image_settings_cvars(initial->image), name, text, true);
  free(text);
}
static void initial_apply_launch(void *ctx) {
  qa_app_prepared_initial *initial = ctx;
  const qa_app_options *options = &initial->options;
  const char *original_mode = options->mode;
  const void *original_skirmish = options->team_arena_skirmish;
  if (initial->team_arena != NULL) {
    size_t seat_count = 0;
    const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(initial->prepared, &seat_count);
    qa_app_team_arena_launch_seat *launch_seats = calloc(seat_count + 1, sizeof *launch_seats);
    if (!launch_seats) qa_range_error("Out of memory");
    for (size_t i = 0; i < seat_count; i++) launch_seats[i] = (qa_app_team_arena_launch_seat){seats[i].id, seats[i].cvars};
    qa_app_team_arena_launch_overrides_apply(initial->team_arena,
        qa_app_prepared_startup_source(initial->prepared), launch_seats, seat_count);
    free(launch_seats);
  }
  const qa_app_q3_map_launch *q3_launch = options->q3_map_launch;
  if (q3_launch != NULL)
    qa_app_apply_q3_map_launch(qa_app_prepared_startup_source(initial->prepared), q3_launch, "all");
  double capacity = initial->args->default_capacity;
  if (options->team_arena_skirmish != NULL)
    capacity = ((const qa_app_team_arena_skirmish *)options->team_arena_skirmish)->max_clients;
  if (q3_launch != NULL && q3_launch->has_max_clients) capacity = q3_launch->max_clients;
  size_t definition_count = 0;
  const qa_server_setting_definition **definitions = qa_server_definitions_for_selection(
      initial->selection.source, initial->selection.match, initial->selection.combat, &definition_count);
  qa_app_startup_rules resolved = qa_app_resolve_startup_rules(*options,
      qa_app_prepared_startup_source(initial->prepared), capacity, definitions, definition_count,
      options->team_arena_skirmish == NULL && q3_launch == NULL);
  free(definitions);
  initial->options = resolved.options;
  initial->max_clients = resolved.max_clients;
  initial->resolved = true;
  if (original_skirmish != NULL) initial->options.mode = original_mode;
  if (initial->image != NULL) {
    qa_cvar_registry *image_cvars = qa_app_image_settings_cvars(initial->image);
    if (initial->options.has_render_worker)
      set_cvar_text(image_cvars, "r_smp", initial->options.render_worker ? "1" : "0", true);
    if (initial->options.has_display_width) set_image_number(initial, "r_customwidth", initial->options.display_width);
    if (initial->options.has_display_height) set_image_number(initial, "r_customheight", initial->options.display_height);
    if (initial->options.has_display_gamma) set_image_number(initial, "r_gamma", initial->options.display_gamma);
  }
}
qa_app_prepared_initial *qa_app_prepare_initial_configuration(qa_app_options *options_in,
    const qa_app_prepare_initial_args *args) {
  qa_app_prepared_initial *initial = calloc(1, sizeof *initial);
  if (!initial) qa_range_error("Out of memory");
  initial->args = args;
  initial->scripts = args->scripts;
  qa_app_options options = *options_in;
  qa_q3_application_product content_q3;
  if (qa_app_configuration_content_q3_value(args->content, &content_q3)) {
    options.has_q3_product = true;
    options.q3_product = content_q3;
  }
  initial->options = options;
  initial->selection = qa_app_configuration_content_selection(args->content);
  qa_command_dialect dialect = qa_app_configuration_dialect_in_catalog(args->catalog, args->content);
  qa_command_dialect movement = qa_app_configuration_movement_dialect(&initial->selection);
  qa_command_context context = {0};
  context.session = qa_engine_session_id(args->session);
  context.origin.kind = QA_COMMAND_ORIGIN_SERVER_CONSOLE;
  const char *campaign = qa_source_program_product(args->catalog, initial->selection.source.content)->expectation.campaign;
  qa_app_application_source_selection source_selection = {initial->selection.source, initial->selection.match,
      options.rules != NULL ? options.rules
          : (campaign != NULL && (strcmp(campaign, "ctf") == 0 || strcmp(campaign, "lrctf") == 0) ? "ctf" : "standard")};
  initial->source = qa_app_create_startup_source(&options, source_selection, dialect,
      context, args->default_capacity, args->print, args->print_ctx);
  if (initial->source == NULL) qa_range_error("Out of memory");
  qa_app_register_gtv_cvars(initial->source);
  qa_cvar_registry_options input_options = {0};
  input_options.dialect = movement;
  input_options.context = context;
  input_options.print = args->print;
  input_options.print_ctx = args->print_ctx;
  initial->input_cvars = qa_cvar_registry_create(&input_options);
  if (initial->input_cvars == NULL) qa_range_error("Out of memory");
  qa_app_audio_preferences audio = {0};
  bool has_audio = qa_app_load_audio_settings_warn(args->settings, &audio, args->print, args->print_ctx);
  qa_app_image_settings_options image_options = {0};
  image_options.defer_persistence = true;
  image_options.context = context;
  image_options.dialect = dialect;
  image_options.has_audio_format = true;
  image_options.audio_output_format = has_audio ? audio.output_format : qa_default_audio_output_format;
  image_options.user_content_root = options.user_content_root;
  image_options.has_render_worker = options.has_render_worker;
  image_options.render_worker = options.render_worker;
  image_options.print = args->print;
  image_options.print_ctx = args->print_ctx;
  initial->image = qa_app_image_settings_open(image_options);
  if (initial->image == NULL) qa_range_error("Out of memory");
  /* sharedArchive: image persistedEntries, then the input-device entries. */
  {
    size_t persisted_count = 0;
    const qa_cvar_archive_entry *persisted = qa_app_image_settings_persisted_entries(initial->image, &persisted_count);
    for (size_t i = 0; i < persisted_count; i++) shared_push(initial, persisted[i].name, persisted[i].value);
  }
  qa_config_store device_store = {NULL};
  if (qa_app_input_device_store(options.user_content_root, &device_store)) {
    size_t device_count = 0;
    qa_cvar_archive_entry *device = qa_app_load_input_device_settings(&device_store, &device_count);
    for (size_t i = 0; i < device_count; i++) shared_push(initial, device[i].name, device[i].value);
    qa_app_cvar_archive_entries_free(device, device_count);
    qa_config_store_destroy(&device_store);
  }
  if (initial->image != NULL) {
    qa_cvar_registry *image_cvars = qa_app_image_settings_cvars(initial->image);
    qa_cvar_read volume = {0};
    bool has_volume = qa_cvar_registry_find(image_cvars, "volume", &volume);
    if (has_volume) qa_cvar_read_free(&volume);
    /* TS loads audio.json again here, with the same warning on an invalid document. */
    qa_app_audio_preferences shared_audio = {0};
    bool has_shared_audio = has_volume
        && qa_app_load_audio_settings_warn(args->settings, &shared_audio, args->print, args->print_ctx);
    if (has_shared_audio) {
      if (shared_audio.has_output_format) {
        qa_app_write_audio_output_cvars(image_cvars, shared_audio.output_format);
        for (size_t i = 0; i < qa_app_audio_output_cvar_name_count; i++) {
          char *value = qa_cvar_registry_variable_string(image_cvars, qa_app_audio_output_cvar_names[i]);
          shared_push(initial, qa_app_audio_output_cvar_names[i], value != NULL ? value : "");
          free(value);
        }
      }
      shared_push_number(initial, "volume", shared_audio.effects_volume);
      shared_push_number(initial, "bgmvolume", shared_audio.music_volume);
      if (shared_audio.has_music_shuffle) shared_push(initial, "music_shuffle", shared_audio.music_shuffle ? "1" : "0");
      if (shared_audio.has_menu_track) shared_push(initial, "music_menu_track", shared_audio.menu_track);
      free(shared_audio.menu_track); free(shared_audio.device_name);
    }
    qa_app_view_settings *view = qa_app_view_settings_create(view_changed, image_cvars);
    if (view == NULL) qa_range_error("Out of memory");
    qa_app_view_settings_load_warn(view, args->settings, args->print, args->print_ctx);
    qa_app_view_settings_bind_cvars(view, image_cvars);
    double override = 0;
    bool has_override = qa_app_view_settings_override(view, &override);
    bool has_fov = false;
    for (size_t i = 0; i < initial->shared_archive_count; i++)
      if (strcmp(initial->shared_archive[i].name, "fov") == 0) has_fov = true;
    if (has_override && !has_fov) shared_push_number(initial, "fov", qa_app_view_settings_field_of_view(view));
    qa_app_view_settings_unbind_cvars(view);
    qa_app_view_settings_destroy(view);
  }
  size_t seat_count = options.dedicated ? 0 : (size_t)options.seats;
  initial->seat_owner_count = seat_count;
  initial->local_seat_count = 0;
  qa_app_prepared_seat_configuration *configs = calloc(seat_count + 1, sizeof *configs);
  initial->seat_cvars = calloc(seat_count + 1, sizeof *initial->seat_cvars);
  initial->mouse_cvars = calloc(seat_count + 1, sizeof *initial->mouse_cvars);
  initial->profiles = calloc(seat_count + 1, sizeof *initial->profiles);
  initial->has_profile = calloc(seat_count + 1, sizeof *initial->has_profile);
  initial->archives = calloc(seat_count + 1, sizeof *initial->archives);
  initial->archive_counts = calloc(seat_count + 1, sizeof *initial->archive_counts);
  initial->mouse_archives = calloc(seat_count + 1, sizeof *initial->mouse_archives);
  initial->mouse_archive_counts = calloc(seat_count + 1, sizeof *initial->mouse_archive_counts);
  initial->local_seats = calloc(seat_count + 1, sizeof *initial->local_seats);
  initial->local_clients = calloc(seat_count + 1, sizeof *initial->local_clients);
  if (!configs || !initial->seat_cvars || !initial->mouse_cvars || !initial->profiles || !initial->has_profile
      || !initial->archives || !initial->archive_counts || !initial->mouse_archives
      || !initial->mouse_archive_counts || !initial->local_seats || !initial->local_clients)
    qa_range_error("Out of memory");
  qa_config_store client_store = {NULL};
  bool has_client_store = seat_count > 0
      && qa_app_configuration_store_for_catalog(args->catalog, &options, initial->selection.engine_behavior.content, &client_store);
  const char *character = qa_game_family_name(options.character);
  for (size_t index = 0; index < seat_count; index++) {
    qa_session_client *client = options.dedicated ? NULL : qa_engine_session_create_client(args->session, (int64_t)index);
    qa_session_seat *local = client == NULL ? NULL : qa_engine_session_create_seat(args->session, (int64_t)index, client);
    if (client != NULL && local != NULL) {
      initial->local_clients[initial->local_seat_count] = qa_session_client_id(client);
      initial->local_seats[initial->local_seat_count] = local;
      initial->local_seat_count++;
    }
    qa_seat_id id = local != NULL ? qa_session_seat_id(local) : qa_identity_owner_seat(args->identity, (int64_t)index);
    qa_command_context seat_context = context;
    if (local != NULL) {
      memset(&seat_context, 0, sizeof seat_context);
      seat_context.session = context.session;
      seat_context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
      seat_context.origin.seat = id;
      seat_context.origin.client = qa_session_client_id(qa_session_seat_client(local));
    }
    qa_cvar_registry_options cvar_options = {0};
    cvar_options.dialect = dialect;
    cvar_options.context = seat_context;
    cvar_options.print = args->print;
    cvar_options.print_ctx = args->print_ctx;
    cvar_options.cheats_allowed = cheats_allowed;
    cvar_options.cheats_allowed_ctx = initial->source;
    qa_cvar_registry *cvars = qa_cvar_registry_create(&cvar_options);
    if (cvars == NULL) qa_range_error("Out of memory");
    initial->seat_cvars[index] = cvars;
    qa_app_register_player_userinfo(cvars, (double)index,
        options.character == QA_GAME_FAMILY_Q2 ? options.character_model : "male");
    qa_app_character_appearance appearance = {character, options.character_model, options.character_skin,
        options.has_character_top_color, options.character_top_color,
        options.has_character_bottom_color, options.character_bottom_color};
    qa_app_apply_character_appearance(cvars, appearance);
    if (dialect == QA_COMMAND_DIALECT_Q3) {
      char player_name[64];
      (void)snprintf(player_name, sizeof player_name, "Player %zu", index + 1);
      qa_app_q3_client_identity identity = {player_name, qa_app_q3_client_model(character, options.character_model)};
      qa_app_initialize_q3_client_cvars(cvars, identity);
    }
    qa_cvar_registry_options mouse_options = {0};
    mouse_options.dialect = dialect;
    mouse_options.context = seat_context;
    mouse_options.print = args->print;
    mouse_options.print_ctx = args->print_ctx;
    qa_cvar_registry *mouse_cvars = qa_cvar_registry_create(&mouse_options);
    if (mouse_cvars == NULL) qa_range_error("Out of memory");
    initial->mouse_cvars[index] = mouse_cvars;
    qa_mouse_settings mouse = {NULL};
    qa_mouse_settings_init(&mouse, mouse_cvars);
    char *profile_name = qa_app_configuration_seat_profile_name(index);
    initial->has_profile[index] = !options.dedicated
        && qa_config_store_load_seat(args->settings, profile_name, &initial->profiles[index]);
    free(profile_name);
    if (!options.dedicated && has_client_store)
      initial->archives[index] = qa_app_configuration_load_client_archive(&client_store,
          initial->selection.engine_behavior.content, initial->selection.engine_behavior.provider,
          index, dialect, &initial->archive_counts[index]);
    if (!options.dedicated)
      initial->mouse_archives[index] = qa_app_configuration_load_input_archive(args->settings, index,
          dialect, &initial->mouse_archive_counts[index]);
    configs[index].context = seat_context;
    configs[index].id = id;
    configs[index].cvars = cvars;
    configs[index].mouse = mouse;
    configs[index].profile = initial->has_profile[index] ? &initial->profiles[index] : NULL;
    configs[index].archive = initial->archives[index];
    configs[index].archive_count = initial->archive_counts[index];
    configs[index].mouse_archive = initial->mouse_archives[index];
    configs[index].mouse_archive_count = initial->mouse_archive_counts[index];
  }
  if (has_client_store) qa_config_store_destroy(&client_store);
  size_t snapshot_count = 0;
  qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(initial->source, 0, &snapshot_count);
  initial->shared_name_count = snapshot_count;
  initial->shared_names = calloc(snapshot_count + 1, sizeof *initial->shared_names);
  if (!initial->shared_names) qa_range_error("Out of memory");
  for (size_t i = 0; i < snapshot_count; i++) {
    initial->shared_names[i] = snapshots[i].name;
    snapshots[i].name = NULL;
    qa_cvar_read_free(&snapshots[i]);
  }
  free(snapshots);
  qa_app_prepared_startup_options startup_options = {0};
  startup_options.startup_commands = (const char *const *)options.startup_commands;
  startup_options.startup_command_count = options.startup_command_count;
  startup_options.has_q3_policy = options.has_q3_product;
  if (options.has_q3_product) startup_options.q3_policy = options.q3_product.policy;
  startup_options.dialect = dialect;
  startup_options.movement_dialect = movement;
  startup_options.seats = configs;
  startup_options.seat_count = seat_count;
  startup_options.shared = initial->image != NULL ? qa_app_image_settings_cvars(initial->image) : NULL;
  startup_options.shared_names = (const char *const *)initial->shared_names;
  startup_options.shared_name_count = initial->shared_name_count;
  startup_options.print = host_print_source;
  startup_options.print_ctx = (void *)args;
  startup_options.forward = request_forward;
  startup_options.forward_ctx = initial;
  initial->prepared = qa_app_prepared_startup_create_concrete(initial->source, initial->input_cvars,
      args->scripts, &startup_options);
  free(configs);
  qa_app_configuration_script_roots script_roots = qa_app_configuration_script_roots_for(args->catalog, &initial->selection);
  if (options.team_arena_skirmish != NULL) {
    initial->team_arena = qa_app_team_arena_launch_overrides_create(options.team_arena_skirmish);
    if (initial->team_arena == NULL) qa_range_error("Out of memory");
  }
  initial->max_clients = args->default_capacity;
  if (args->on_prepared) args->on_prepared(args->on_prepared_ctx, initial->prepared);
  if (!options.dedicated) {
    initial->movement_archive = qa_app_configuration_load_movement_archive(args->settings, movement, &initial->movement_archive_count);
    initial->fallback_archive = qa_app_configuration_load_fallback_archive(args->settings, dialect, &initial->fallback_archive_count);
  }
  qa_app_prepared_scoped_read_fn read = NULL;
  void *read_ctx = NULL;
  qa_app_configuration_script_reader(args->catalog, &initial->selection, &options, args->scripts, &read, &read_ctx);
  initial->read_ctx = read_ctx;
  qa_app_prepared_execute_options execute = {0};
  execute.read = read;
  execute.read_ctx = read_ctx;
  execute.has_mod = script_roots.has_mod;
  execute.apply_launch_options = initial_apply_launch;
  execute.apply_ctx = initial;
  execute.next_frame = args->next_frame;
  execute.next_frame_ctx = args->next_frame_ctx;
  execute.source_archive = args->source_archive;
  execute.source_archive_count = args->source_archive_count;
  execute.shared_archive = initial->shared_archive;
  execute.shared_archive_count = initial->shared_archive_count;
  execute.movement_archive = initial->movement_archive;
  execute.movement_archive_count = initial->movement_archive_count;
  execute.fallback_archive = initial->fallback_archive;
  execute.fallback_archive_count = initial->fallback_archive_count;
  qa_app_prepared_startup_execute(initial->prepared, &execute);
  free(audio.menu_track);
  free(audio.device_name);
  return initial;
}
qa_app_prepared_startup *qa_app_prepared_initial_startup(qa_app_prepared_initial *initial) { return initial->prepared; }
qa_app_image_settings *qa_app_prepared_initial_image(qa_app_prepared_initial *initial) { return initial->image; }
const qa_app_options *qa_app_prepared_initial_options(const qa_app_prepared_initial *initial) { return &initial->options; }
double qa_app_prepared_initial_max_clients(const qa_app_prepared_initial *initial) { return initial->max_clients; }
const qa_app_configuration_command_request *qa_app_prepared_initial_requests(
    const qa_app_prepared_initial *initial, size_t *out_count) {
  *out_count = initial->request_count;
  return initial->requests;
}
qa_app_console_script_files *qa_app_prepared_initial_scripts(qa_app_prepared_initial *initial) { return initial->scripts; }
qa_session_seat *qa_app_prepared_initial_local_seat(qa_app_prepared_initial *initial, qa_client_id client) {
  for (size_t i = 0; i < initial->local_seat_count; i++)
    if (qa_same_client(initial->local_clients[i], client)) return initial->local_seats[i];
  return NULL;
}
void qa_app_prepared_initial_destroy(qa_app_prepared_initial *initial) {
  if (initial == NULL) return;
  if (initial->prepared) qa_app_prepared_startup_close(initial->prepared);
  if (initial->team_arena) qa_app_team_arena_launch_overrides_destroy(initial->team_arena);
  qa_app_configuration_script_reader_free(initial->read_ctx);
  if (initial->image) qa_app_image_settings_destroy(initial->image);
  for (size_t i = 0; i < initial->seat_owner_count; i++) {
    if (initial->seat_cvars && initial->seat_cvars[i]) qa_cvar_registry_destroy(initial->seat_cvars[i]);
    if (initial->mouse_cvars && initial->mouse_cvars[i]) qa_cvar_registry_destroy(initial->mouse_cvars[i]);
    if (initial->has_profile && initial->has_profile[i]) qa_seat_settings_free(&initial->profiles[i]);
    if (initial->archives) qa_app_cvar_archive_entries_free(initial->archives[i], initial->archive_counts[i]);
    if (initial->mouse_archives) qa_app_cvar_archive_entries_free(initial->mouse_archives[i], initial->mouse_archive_counts[i]);
  }
  if (initial->source) qa_cvar_registry_destroy(initial->source);
  if (initial->input_cvars) qa_cvar_registry_destroy(initial->input_cvars);
  for (size_t i = 0; i < initial->request_count; i++) {
    free((void *)initial->requests[i].name);
    for (size_t j = 0; j < initial->requests[i].argument_count; j++) free((void *)initial->requests[i].arguments[j]);
    free((void *)initial->requests[i].arguments);
    qa_command_context_release(&initial->requests[i].source);
  }
  free(initial->requests);
  for (size_t i = 0; i < initial->shared_archive_count; i++) {
    free(initial->shared_archive[i].name);
    free(initial->shared_archive[i].value);
  }
  free(initial->shared_archive);
  qa_app_cvar_archive_entries_free(initial->movement_archive, initial->movement_archive_count);
  qa_app_cvar_archive_entries_free(initial->fallback_archive, initial->fallback_archive_count);
  for (size_t i = 0; i < initial->shared_name_count; i++) free(initial->shared_names[i]);
  free(initial->shared_names);
  free(initial->seat_cvars);
  free(initial->mouse_cvars);
  free(initial->profiles);
  free(initial->has_profile);
  free(initial->archives);
  free(initial->archive_counts);
  free(initial->mouse_archives);
  free(initial->mouse_archive_counts);
  free(initial->local_seats);
  free(initial->local_clients);
  free(initial);
}
/* --- prepareProfileConfiguration --- */
typedef struct profile_choice {
  qa_seat_id id;
  qa_input_binding *authored; size_t authored_count; bool has_authored;
  char **overridden; size_t overridden_count, overridden_capacity;
  bool all;
  const qa_input_binding *selected; size_t selected_count;
  qa_input_binding *owned_selected; size_t owned_selected_count;
} profile_choice;
typedef struct profile_retained {
  bool present;
  qa_input_binding *bindings; size_t binding_count;
  qa_mouse_tuning mouse;
  bool has_run; char *run;
  bool all;
  char **overridden; size_t overridden_count;
  const qa_input_binding *selected; size_t selected_count;
  qa_input_binding *owned_selected; size_t owned_selected_count;
} profile_retained;
typedef struct profile_binding_list { qa_input_binding *bindings; size_t count; } profile_binding_list;
static void profile_bindings_dispose(qa_input_binding *bindings, size_t count) {
  if (bindings == NULL) return;
  for (size_t i = 0; i < count; i++) qa_input_binding_dispose(&bindings[i]);
  free(bindings);
}
typedef struct profile_named {
  qa_app_prepared_profile *profile;
  const char *name;
} profile_named;
struct qa_app_prepared_profile {
  const qa_app_prepare_profile_args *args;
  qa_app_options options;
  qa_app_application_configuration_selection selection;
  qa_command_dialect dialect, movement_dialect;
  qa_app_startup_phases phases; bool has_phases;
  qa_cvar_registry *source, *movement, *fallback;
  bool owns_source, owns_fallback;
  qa_app_prepared_seat_configuration *seats; size_t seat_count;
  qa_seat_input **inputs;
  qa_cvar_registry **owned_seat_cvars, **mouse_cvars;
  qa_seat_settings *profiles; bool *has_profile;
  qa_app_prepared_client_commands *program;
  qa_app_console_routing *routing;
  qa_app_prepared_scoped_read_fn read; void *read_ctx;
  qa_app_configuration_command_request *requests; size_t request_count, request_capacity;
  qa_app_profile_forward_fn forward; void *forward_ctx;
  bool stopped, complete;
  qa_app_prepared_startup *published;
  qa_app_startup_config *active;
  profile_choice *choices;
  profile_retained *retained;
  /* run() generator state. */
  int stage; /* 0 start, 1 seats, 2 late, 3 done */
  size_t index; bool seat_started, collecting, pending_advance, late_appended;
  qa_input_binding *seat_defaults; size_t seat_default_count; bool seat_defaults_owned;
  /* Default binding lists the run created: the choices and the prepared startup's selected lists hold their binding
   * values (TS shares binding objects), so they live until the profile is destroyed. */
  profile_binding_list *default_lists; size_t default_list_count;
  const qa_catalog_discovered_product *product, *base;
  qa_cvar_archive_entry *movement_archive; size_t movement_archive_count;
  qa_cvar_archive_entry *fallback_archive; size_t fallback_archive_count;
  qa_cvar_archive_entry *shared_archive; size_t shared_archive_count;
  profile_named **named; size_t named_count, named_capacity;
  qa_app_q1_client_commands *q1_registration;
  qa_app_q2_client_commands *q2_registration;
  /* Routing server() snapshot-name storage (rebuilt per call). */
  char **server_names; size_t server_name_count;
  double max_clients;
};
static void *profile_xmalloc(size_t size) {
  void *ptr = malloc(size == 0 ? 1 : size);
  if (!ptr) qa_range_error("Out of memory");
  return ptr;
}
static void *profile_xcalloc(size_t count, size_t size) {
  void *ptr = calloc(count == 0 ? 1 : count, size);
  if (!ptr) qa_range_error("Out of memory");
  return ptr;
}
static char *profile_dup(const char *text) {
  char *copy = strdup(text);
  if (!copy) qa_range_error("Out of memory");
  return copy;
}
static qa_input_binding *profile_copy_bindings(const qa_input_binding *bindings, size_t count) {
  qa_input_binding *copy = profile_xmalloc((count + 1) * sizeof *copy);
  if (count > 0) memcpy(copy, bindings, count * sizeof *copy);
  return copy;
}
static void choice_overridden_add(profile_choice *choice, const char *key) {
  for (size_t i = 0; i < choice->overridden_count; i++)
    if (strcmp(choice->overridden[i], key) == 0) return;
  if (choice->overridden_count == choice->overridden_capacity) {
    size_t capacity = choice->overridden_capacity == 0 ? 8 : choice->overridden_capacity * 2;
    char **grown = realloc(choice->overridden, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    choice->overridden = grown;
    choice->overridden_capacity = capacity;
  }
  choice->overridden[choice->overridden_count++] = profile_dup(key);
}
static bool choice_overridden_has(const profile_choice *choice, const char *key) {
  for (size_t i = 0; i < choice->overridden_count; i++)
    if (strcmp(choice->overridden[i], key) == 0) return true;
  return false;
}
static profile_choice *profile_choice_for(qa_app_prepared_profile *profile, qa_seat_id id) {
  for (size_t i = 0; i < profile->seat_count; i++)
    if (qa_same_seat(profile->choices[i].id, id)) return &profile->choices[i];
  return NULL;
}
/* TS Map<physicalInputKey, InputBinding> value list. */
typedef struct profile_binding_map {
  char **keys; qa_input_binding *values; size_t count, capacity;
} profile_binding_map;
static void profile_map_set(profile_binding_map *map, qa_input_binding binding) {
  char *key = qa_physical_input_key(&binding.input);
  if (!key) qa_range_error("Out of memory");
  for (size_t i = 0; i < map->count; i++)
    if (strcmp(map->keys[i], key) == 0) { map->values[i] = binding; free(key); return; }
  if (map->count == map->capacity) {
    size_t capacity = map->capacity == 0 ? 8 : map->capacity * 2;
    char **keys = realloc(map->keys, capacity * sizeof *keys);
    qa_input_binding *values = realloc(map->values, capacity * sizeof *values);
    if (!keys || !values) qa_range_error("Out of memory");
    map->keys = keys; map->values = values; map->capacity = capacity;
  }
  map->keys[map->count] = key;
  map->values[map->count] = binding;
  map->count++;
}
static bool profile_map_has(profile_binding_map *map, const char *key) {
  for (size_t i = 0; i < map->count; i++)
    if (strcmp(map->keys[i], key) == 0) return true;
  return false;
}
static void profile_map_free(profile_binding_map *map) {
  for (size_t i = 0; i < map->count; i++) free(map->keys[i]);
  free(map->keys);
  free(map->values);
  memset(map, 0, sizeof *map);
}
static qa_input_binding *profile_map_values(profile_binding_map *map, size_t *out_count) {
  *out_count = map->count;
  return profile_copy_bindings(map->values, map->count);
}
/* TS currentCommands / currentSeat / currentBindings. */
static qa_command_buffer *profile_commands(qa_app_prepared_profile *profile) {
  return profile->published != NULL ? qa_app_prepared_startup_commands(profile->published)
      : qa_app_prepared_client_commands_buffer(profile->program);
}
/* name => currentCommands().exists(name) */
static bool profile_command_exists(void *ctx, const char *name) {
  return qa_command_buffer_exists(profile_commands(ctx), name);
}
/* TS append: (text, context) => currentCommands().appendPreparation(text, context, dialect). */
static void profile_append_preparation(void *ctx, qa_command_buffer *commands, const char *text, const qa_command_context *context) {
  (void)commands; /* TS reads currentCommands() at append time */
  qa_app_prepared_profile *profile = ctx;
  qa_command_buffer_append_preparation(profile_commands(profile), text, context, profile->dialect);
}
typedef struct profile_current_seat {
  qa_cvar_registry *cvars;
  qa_mouse_settings *mouse;
  qa_seat_input *input;
} profile_current_seat;
static profile_current_seat profile_current(qa_app_prepared_profile *profile, qa_seat_id id) {
  if (profile->published != NULL) {
    size_t count = 0;
    const qa_app_prepared_seat *seats = qa_app_prepared_startup_seats(profile->published, &count);
    for (size_t i = 0; i < count; i++)
      if (qa_same_seat(seats[i].id, id)) {
        profile_current_seat out = {seats[i].cvars, (qa_mouse_settings *)&seats[i].mouse, seats[i].input};
        return out;
      }
  } else {
    for (size_t i = 0; i < profile->seat_count; i++)
      if (qa_same_seat(profile->seats[i].id, id)) {
        profile_current_seat out = {profile->seats[i].cvars, &profile->seats[i].mouse, profile->inputs[i]};
        return out;
      }
  }
  qa_range_error("Configuration seat has retired");
}
static bool profile_bindings(qa_app_prepared_profile *profile, qa_seat_id id, qa_binding_command_seat *out) {
  if (profile->published == NULL)
    return qa_app_prepared_client_commands_input(profile->program, id, out);
  profile_current_seat seat = profile_current(profile, id);
  if (seat.input == NULL) return false;
  *out = qa_seat_input_command_seat(seat.input);
  return true;
}
static void profile_publish_choices(qa_app_prepared_profile *profile, qa_seat_id id) {
  profile_choice *choices = profile_choice_for(profile, id);
  if (profile->published == NULL || choices == NULL) return;
  qa_app_prepared_startup_publish_seat_choices(profile->published, id, choices->all,
      true, choices->selected, choices->selected_count,
      choices->has_authored, choices->authored, choices->authored_count,
      (const char *const *)choices->overridden, choices->overridden_count);
}
static void profile_apply_defaults(qa_app_prepared_profile *profile, qa_seat_id id,
    const qa_input_binding *defaults, size_t default_count) {
  profile_choice *choices = profile_choice_for(profile, id);
  qa_binding_command_seat bindings = {0};
  bool has_bindings = profile_bindings(profile, id, &bindings);
  if (choices == NULL || !has_bindings) qa_range_error("Profile defaults belong to another seat");
  choices->selected = defaults;
  choices->selected_count = default_count;
  profile_publish_choices(profile, id);
  if (!choices->all) {
    for (size_t i = 0; i < default_count; i++) {
      char *key = qa_physical_input_key(&defaults[i].input);
      if (!key) qa_range_error("Out of memory");
      bool overridden = choice_overridden_has(choices, key);
      free(key);
      if (!overridden && !bindings.bind(bindings.ctx, &defaults[i])) qa_range_error("Out of memory");
    }
  }
}
/* TS forward/dispatch. Requests are deep-copied while collecting. */
static void profile_collect_request(qa_app_prepared_profile *profile,
    const qa_app_configuration_command_request *incoming) {
  qa_app_configuration_command_request request = *incoming;
  if (profile->request_count == profile->request_capacity) {
    size_t capacity = profile->request_capacity == 0 ? 8 : profile->request_capacity * 2;
    qa_app_configuration_command_request *grown = realloc(profile->requests, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    profile->requests = grown;
    profile->request_capacity = capacity;
  }
  request.name = profile_dup(request.name);
  const char **copied = NULL;
  if (request.argument_count > 0) {
    copied = profile_xmalloc(request.argument_count * sizeof *copied);
    for (size_t i = 0; i < request.argument_count; i++) copied[i] = profile_dup(request.arguments[i]);
  }
  request.arguments = copied;
  request.source = qa_command_context_copy(&request.source); /* the origin chain dies with its command */
  profile->requests[profile->request_count++] = request;
}
static void profile_dispatch(qa_app_prepared_profile *profile, const char *name,
    const char *const *arguments, size_t argument_count, qa_command_context source) {
  profile->stopped = true;
  qa_app_configuration_command_request request =
      qa_app_configuration_make_request(name, arguments, argument_count, source);
  if (profile->forward != NULL) profile->forward(profile->forward_ctx, &request);
  else profile_collect_request(profile, &request);
}
static void profile_named_handler(void *ctx, qa_command_invocation *command) {
  profile_named *named = ctx;
  size_t count = command->argc > 0 ? command->argc - 1 : 0;
  profile_dispatch(named->profile, named->name,
      count > 0 ? (const char *const *)(command->argv + 1) : NULL, count, command->source);
}
static void profile_forward_to_server(void *ctx, qa_command_invocation *command) {
  qa_app_prepared_profile *profile = ctx;
  size_t count = command->argc > 0 ? command->argc - 1 : 0;
  profile_dispatch(profile, command->argc > 0 ? command->argv[0] : "",
      count > 0 ? (const char *const *)(command->argv + 1) : NULL, count, command->source);
}
static void profile_execute_client(void *ctx, const char *name, const char *const *arguments,
    size_t argument_count, const qa_seat_id *seat, qa_command_context source) {
  (void)seat;
  profile_dispatch(ctx, name, arguments, argument_count, source);
}
static qa_script_read_result profile_read_script(void *ctx, const char *name, qa_command_context source) {
  qa_app_prepared_profile *profile = ctx;
  qa_script_read_result result = {0};
  result.kind = QA_SCRIPT_READ_READY;
  result.has_text = true;
  result.text = profile->active != NULL ? qa_app_startup_config_read_script(profile->active, name, source)
      : qa_app_console_script_files_read(profile->args->scripts, name, source);
  if (result.text == NULL) result.has_text = false;
  return result;
}
static void profile_script_complete(void *ctx, const qa_script_completion *event) {
  qa_app_prepared_profile *profile = ctx;
  if (profile->active != NULL) qa_app_startup_config_on_script_complete(profile->active, event);
}
static void profile_print_source(void *ctx, const char *text, const qa_command_context *source) {
  qa_app_prepared_profile *profile = ctx;
  (void)source;
  if (profile->args->print) profile->args->print(profile->args->print_ctx, text);
}
static bool profile_allow_command(void *ctx, qa_command_invocation *command) {
  qa_app_prepared_profile *profile = ctx;
  if (profile->active == NULL || !qa_app_startup_config_restrict_shared_configuration(profile->active))
    return true;
  qa_app_prepared_seat *seats = profile_xcalloc(profile->seat_count + 1, sizeof *seats);
  for (size_t i = 0; i < profile->seat_count; i++) {
    seats[i].context = profile->seats[i].context;
    seats[i].id = profile->seats[i].id;
    seats[i].cvars = profile->seats[i].cvars;
    seats[i].mouse = profile->seats[i].mouse;
    seats[i].input = profile->inputs[i];
  }
  bool allowed = qa_app_allow_seat_configuration_command_for(command,
      qa_app_console_routing_view(profile->routing), seats, profile->seat_count,
      profile_print_source, profile);
  free(seats);
  return allowed;
}
/* TS ApplicationConsoleRouting option closures. */
static qa_command_dialect profile_routing_dialect(void *ctx) {
  return ((qa_app_prepared_profile *)ctx)->dialect;
}
static bool profile_routing_server(void *ctx, qa_app_console_server *out) {
  qa_app_prepared_profile *profile = ctx;
  if (profile->args->client_source != NULL) return false;
  for (size_t i = 0; i < profile->server_name_count; i++) free(profile->server_names[i]);
  free(profile->server_names);
  profile->server_names = NULL;
  profile->server_name_count = 0;
  size_t count = 0;
  qa_cvar_snapshot *snapshots = qa_cvar_registry_snapshots(profile->source, 0, &count);
  profile->server_names = profile_xcalloc(count + 1, sizeof *profile->server_names);
  for (size_t i = 0; i < count; i++) {
    profile->server_names[i] = snapshots[i].name;
    snapshots[i].name = NULL;
    qa_cvar_read_free(&snapshots[i]);
  }
  free(snapshots);
  profile->server_name_count = count;
  out->cvars = profile->source;
  out->shared_names = (const char *const *)profile->server_names;
  out->shared_name_count = count;
  return true;
}
static qa_cvar_registry *profile_routing_seat(void *ctx, qa_seat_id id) {
  qa_app_prepared_profile *profile = ctx;
  for (size_t i = 0; i < profile->seat_count; i++)
    if (qa_same_seat(profile->seats[i].id, id)) return profile->seats[i].cvars;
  return NULL;
}
static qa_cvar_registry *profile_routing_input(void *ctx, bool has_seat, qa_seat_id id) {
  qa_app_prepared_profile *profile = ctx;
  for (size_t i = 0; i < profile->seat_count; i++)
    if (!has_seat || qa_same_seat(profile->seats[i].id, id)) return profile->seats[i].mouse.cvars;
  return NULL;
}
static qa_cvar_registry *profile_routing_movement(void *ctx) {
  return ((qa_app_prepared_profile *)ctx)->movement;
}
static qa_cvar_registry *profile_routing_shared(void *ctx) {
  qa_app_prepared_profile *profile = ctx;
  qa_cvar_registry *shared = profile->published != NULL
      ? qa_app_prepared_startup_shared_cvars(profile->published) : NULL;
  return shared != NULL ? shared : profile->args->shared;
}
/* --- TS run() generator as a step machine (one yield per step). --- */
static qa_command_context profile_launch_context(qa_app_prepared_profile *profile) {
  return profile->seat_count > 0 ? profile->seats[0].context
      : qa_command_buffer_context(qa_app_prepared_startup_commands(profile->args->prepared));
}
static void profile_apply_startup_variables(qa_app_prepared_profile *profile) {
  qa_command_context launch = profile_launch_context(profile);
  for (size_t i = 0; i < profile->phases.variable_count; i++) {
    qa_app_startup_variable *variable = &profile->phases.variables[i];
    qa_cvar_registry *owner = qa_app_console_routing_owner(profile->routing, variable->name, launch);
    set_cvar_text(owner, variable->name, variable->value, true);
    qa_cvar_snapshot registered = {0};
    if (!qa_cvar_registry_register(owner, variable->name, "", 0, &registered))
      qa_range_error("Q3 startup variable registration rejected: %s", variable->name);
    qa_cvar_registry_add_flags(owner, registered.name, QA_CVAR_FLAG_USER_CREATED);
    qa_cvar_read_free(&registered);
  }
}
static char *profile_scoped_read(void *ctx, const char *name, qa_command_context source,
    qa_app_startup_script_scope scope) {
  qa_app_prepared_profile *profile = ctx;
  if (profile->published != NULL)
    return qa_app_prepared_startup_read_configuration(profile->published, name, source, scope);
  return profile->read(profile->read_ctx, name, source, scope);
}
static bool profile_weapon_text(const qa_input_binding *binding) {
  if (binding->target.is_action || binding->target.text == NULL) return false;
  const char *text = binding->target.text;
  const char *words[] = {"weapon", "impulse", "use"};
  for (size_t w = 0; w < 3; w++) {
    size_t length = strlen(words[w]);
    bool match = true;
    /* TS /^(?:weapon|impulse|use)\s/i: ASCII case folding; \s is JS white space. */
    for (size_t i = 0; i < length && match; i++)
      if (tolower((unsigned char)text[i]) != words[w][i]) match = false;
    if (match && qa_js_white_space_length(text + length) != 0) return true;
  }
  return false;
}
static qa_binding_command_seat profile_seat_bindings(qa_app_prepared_profile *profile) {
  qa_binding_command_seat bindings = {0};
  if (!profile_bindings(profile, profile->seats[profile->index].id, &bindings))
    qa_range_error("Prepared configuration has no binding owner");
  return bindings;
}
static void profile_apply_selected_defaults(void *ctx) {
  qa_app_prepared_profile *profile = ctx;
  size_t index = profile->index;
  profile_choice *choices = &profile->choices[index];
  qa_binding_command_seat bindings = profile_seat_bindings(profile);
  size_t current_count = 0;
  const qa_input_binding *current = bindings.bindings(bindings.ctx, &current_count);
  qa_input_binding *snapshot = profile_copy_bindings(current, current_count);
  for (size_t i = 0; i < current_count; i++)
    if (profile_weapon_text(&snapshot[i])) bindings.unbind(bindings.ctx, &snapshot[i].input);
  free(snapshot);
  profile_binding_map authored = {0};
  current = bindings.bindings(bindings.ctx, &current_count);
  for (size_t i = 0; i < current_count; i++) profile_map_set(&authored, current[i]);
  for (size_t i = 0; i < profile->seat_default_count; i++) {
    if (!bindings.bind(bindings.ctx, &profile->seat_defaults[i])) qa_range_error("Out of memory");
    char *key = qa_physical_input_key(&profile->seat_defaults[i].input);
    if (!key) qa_range_error("Out of memory");
    if (!profile_map_has(&authored, key)) profile_map_set(&authored, profile->seat_defaults[i]);
    free(key);
  }
  free(choices->authored);
  choices->authored = profile_map_values(&authored, &choices->authored_count);
  choices->has_authored = true;
  profile_map_free(&authored);
  profile->collecting = true;
  profile_publish_choices(profile, profile->seats[index].id);
}
static bool profile_target_equal(const qa_input_binding_target *left, bool has_left,
    const qa_input_binding_target *right, bool has_right) {
  if (has_left != has_right) return false;
  if (!has_left) return true;
  if (left->is_action != right->is_action) return false;
  if (left->is_action) return left->action == right->action;
  if (left->text == NULL || right->text == NULL) return left->text == right->text;
  return strcmp(left->text, right->text) == 0;
}
static void profile_apply_archive(void *ctx) {
  qa_app_prepared_profile *profile = ctx;
  size_t index = profile->index;
  qa_seat_id id = profile->seats[index].id;
  profile_choice *choices = &profile->choices[index];
  const qa_app_profile_client_source *client = profile->args->client_source;
  if (index == 0) {
    qa_cvar_registry *source = profile->published != NULL
        ? qa_app_prepared_startup_source(profile->published) : profile->source;
    qa_cvar_registry *movement = profile->published != NULL
        ? qa_app_prepared_startup_movement(profile->published) : profile->movement;
    qa_cvar_registry *fallback = profile->published != NULL
        ? qa_app_prepared_startup_fallback(profile->published) : profile->fallback;
    qa_cvar_registry *shared = profile->published != NULL
        ? qa_app_prepared_startup_shared_cvars(profile->published) : profile->args->shared;
    if (client == NULL)
      qa_cvar_registry_apply_archive(source, profile->args->source_archive, profile->args->source_archive_count);
    qa_cvar_registry_apply_archive(movement, profile->movement_archive, profile->movement_archive_count);
    qa_cvar_registry_apply_archive(fallback, profile->fallback_archive, profile->fallback_archive_count);
    if (shared != NULL)
      qa_cvar_registry_apply_archive(shared, profile->shared_archive, profile->shared_archive_count);
  }
  profile_current_seat seat = profile_current(profile, id);
  qa_cvar_registry_apply_archive(seat.cvars, profile->seats[index].archive, profile->seats[index].archive_count);
  qa_cvar_registry_apply_archive(seat.mouse->cvars, profile->seats[index].mouse_archive,
      profile->seats[index].mouse_archive_count);
  if (client != NULL) {
    const qa_cvar_archive_entry *entries = NULL;
    size_t count = 0;
    if (client->has_seat_archives && client->seat_archives != NULL && client->seat_archives[index] != NULL) {
      entries = client->seat_archives[index];
      count = client->seat_archive_counts[index];
    } else if (index == 0) {
      entries = profile->args->source_archive;
      count = profile->args->source_archive_count;
    }
    qa_cvar_registry_apply_archive(seat.cvars, entries, count);
  }
  profile_retained *retained = &profile->retained[index];
  if (retained->present) {
    qa_binding_command_seat bindings = profile_seat_bindings(profile);
    bindings.unbind_all(bindings.ctx);
    for (size_t i = 0; i < retained->binding_count; i++)
      if (!bindings.bind(bindings.ctx, &retained->bindings[i])) qa_range_error("Out of memory");
    choices->all = retained->all;
    for (size_t i = 0; i < retained->overridden_count; i++) choice_overridden_add(choices, retained->overridden[i]);
    /* TS compares JSON.stringify of expected/current targets per key. */
    for (size_t pass = 0; pass < 2; pass++) {
      const qa_input_binding *list = pass == 0 ? retained->selected : retained->bindings;
      size_t list_count = pass == 0 ? retained->selected_count : retained->binding_count;
      for (size_t i = 0; i < list_count; i++) {
        char *key = qa_physical_input_key(&list[i].input);
        if (!key) qa_range_error("Out of memory");
        const qa_input_binding_target *expected = NULL, *current = NULL;
        for (size_t j = 0; j < retained->selected_count; j++) {
          char *other = qa_physical_input_key(&retained->selected[j].input);
          if (other && strcmp(other, key) == 0) expected = &retained->selected[j].target;
          free(other);
        }
        for (size_t j = 0; j < retained->binding_count; j++) {
          char *other = qa_physical_input_key(&retained->bindings[j].input);
          if (other && strcmp(other, key) == 0) current = &retained->bindings[j].target;
          free(other);
        }
        if (!profile_target_equal(expected, expected != NULL, current, current != NULL))
          choice_overridden_add(choices, key);
        free(key);
      }
    }
    profile_current_seat current_seat = profile_current(profile, id);
    qa_mouse_settings_write(current_seat.mouse, retained->mouse);
    if (retained->has_run)
      qa_cvar_registry_set_command_flags(current_seat.mouse->cvars, "cl_run", retained->run,
          QA_CVAR_COMMAND_FLAG_ARCHIVE);
  } else if (profile->has_profile[index]) {
    const qa_seat_settings *seat_profile = &profile->profiles[index];
    qa_binding_command_seat bindings = profile_seat_bindings(profile);
    bindings.unbind_all(bindings.ctx);
    for (size_t i = 0; i < seat_profile->binding_count; i++)
      if (!bindings.bind(bindings.ctx, &seat_profile->bindings[i])) qa_range_error("Out of memory");
    choices->all = true;
    profile_current_seat current_seat = profile_current(profile, id);
    qa_mouse_settings_write(current_seat.mouse, seat_profile->mouse);
    if (seat_profile->has_always_run)
      qa_cvar_registry_set_command_flags(current_seat.mouse->cvars, "cl_run",
          seat_profile->always_run ? "1" : "0", QA_CVAR_COMMAND_FLAG_ARCHIVE);
  }
  if (index == 0 && client != NULL && client->archive_text != NULL) {
    qa_command_origin script_origin = {0};
    qa_command_origin caller = profile->seats[index].context.origin;
    script_origin.kind = QA_COMMAND_ORIGIN_SCRIPT;
    script_origin.name = "client.cfg";
    script_origin.caller = &caller;
    qa_command_context context = profile->seats[index].context;
    context.origin = script_origin;
    /* Saved settings are data: archiveDataText keeps only variable and binding lines. */
    char *archive = qa_app_archive_data_text(client->archive_text, profile_command_exists, profile);
    qa_cleanup archive_guard;
    qa_cleanup_push(&archive_guard, free, archive);
    qa_command_buffer_insert(profile_commands(profile), archive, &context, -1);
    qa_cleanup_pop(&archive_guard);
    free(archive);
  }
  profile->collecting = true;
}
static void profile_apply_launch_noop(void *ctx) { (void)ctx; }
static void profile_replay_variables(void *ctx) {
  qa_app_prepared_profile *profile = ctx;
  if (profile->index != 0 || !profile->has_phases) return;
  profile_apply_startup_variables(profile);
  qa_command_context launch = profile_launch_context(profile);
  qa_command_buffer_insert(profile_commands(profile), profile->phases.early, &launch, -1);
}
static void profile_collect(void *ctx) {
  qa_app_prepared_profile *profile = ctx;
  if (!profile->collecting) return;
  profile_choice *choices = &profile->choices[profile->index];
  size_t count = 0;
  char *const *tokens = qa_command_buffer_tokenized_arguments(profile_commands(profile), &count);
  char *name = qa_ascii_fold(count > 0 && tokens[0] != NULL ? tokens[0] : "");
  if (!name) qa_range_error("Out of memory");
  const char *key = count > 1 ? tokens[1] : NULL;
  if (strcmp(name, "unbindall") == 0) choices->all = true;
  if ((strcmp(name, "bind") == 0 || strcmp(name, "unbind") == 0) && key != NULL) {
    qa_physical_input input = {0};
    if (qa_named_physical_input(key, 0, &input)) {
      char *physical_key = qa_physical_input_key(&input);
      if (!physical_key) qa_range_error("Out of memory");
      choice_overridden_add(choices, physical_key);
      free(physical_key);
    }
  }
  free(name);
  profile_publish_choices(profile, profile->seats[profile->index].id);
}
static bool profile_should_continue(void *ctx) {
  qa_app_prepared_profile *profile = ctx;
  return profile->published == NULL ? !profile->stopped
      : qa_app_prepared_startup_configuration_can_continue(profile->published);
}
static bool profile_run_step(qa_app_prepared_profile *profile) {
  if (profile->stage == 3) return true;
  qa_command_context launch = profile_launch_context(profile);
  if (profile->stage == 0) {
    profile->stage = 1;
    if (profile->has_phases) {
      profile_apply_startup_variables(profile);
      qa_command_buffer_append_preparation(profile_commands(profile), profile->phases.early, &launch, profile->dialect);
      (void)qa_command_buffer_execute_scripts_async(profile_commands(profile), NULL, NULL, NULL, NULL);
    }
  }
  if (profile->stage == 1) {
    if (profile->index >= profile->seat_count) {
      profile->stage = 2;
    } else {
      size_t index = profile->index;
      if (!profile->seat_started) {
        profile->seat_started = true;
        profile_choice *choices = &profile->choices[index];
        profile->collecting = index != 0;
        if (profile->args->seats[index].has_selected) {
          profile->seat_defaults = (qa_input_binding *)profile->args->seats[index].selected_bindings;
          profile->seat_default_count = profile->args->seats[index].selected_count;
          profile->seat_defaults_owned = false;
        } else {
          profile->seat_defaults = qa_default_bindings(0, profile->movement_dialect, NULL, 0,
              &profile->seat_default_count);
          if (profile->seat_defaults == NULL) qa_range_error("Out of memory");
          profile->seat_defaults_owned = true;
        }
        if (index != 0) {
          profile_apply_defaults(profile, profile->seats[index].id,
              profile->seat_defaults, profile->seat_default_count);
          profile_binding_map baseline = {0};
          profile_choice *primary = &profile->choices[0];
          if (primary->has_authored)
            for (size_t i = 0; i < primary->authored_count; i++) profile_map_set(&baseline, primary->authored[i]);
          for (size_t i = 0; i < profile->seat_default_count; i++) {
            char *key = qa_physical_input_key(&profile->seat_defaults[i].input);
            if (!key) qa_range_error("Out of memory");
            if (!profile_map_has(&baseline, key)) profile_map_set(&baseline, profile->seat_defaults[i]);
            free(key);
          }
          free(choices->authored);
          choices->authored = profile_map_values(&baseline, &choices->authored_count);
          choices->has_authored = true;
          profile_map_free(&baseline);
        }
        if (index == 0) {
          qa_binding_command_seat bindings = profile_seat_bindings(profile);
          bindings.unbind_all(bindings.ctx);
        }
        qa_app_startup_config_options config = {0};
        config.dialect = profile->dialect;
        config.context = profile->seats[index].context;
        config.has_mod = strcmp(profile->product->expectation.content_directory,
            profile->base->expectation.content_directory) != 0;
        config.scope = index == 0 ? "source" : "seat";
        config.safe_mode = profile->has_phases && profile->phases.safe;
        config.read = profile_scoped_read;
        config.read_ctx = profile;
        config.apply_ctx = profile;
        config.apply_selected_defaults = profile_apply_selected_defaults;
        config.apply_archive = profile_apply_archive;
        config.apply_launch_options = profile_apply_launch_noop;
        config.replay_startup_variables = profile_replay_variables;
        profile->active = qa_app_startup_config_create(&config);
        if (profile->active == NULL) qa_range_error("Out of memory");
      }
      qa_app_startup_config_append preparation = { profile_append_preparation, profile };
      bool done = qa_app_startup_config_execute_frame(profile->active, profile_commands(profile), preparation,
          profile_collect, profile, profile_should_continue, profile);
      if (!done) return false;
      qa_app_startup_config_destroy(profile->active);
      profile->active = NULL;
      /* Strings stay alive with the authored/staged copies, as the
       * TS shares binding objects (see prepared_startup.c). */
      if (profile->seat_defaults_owned) {
        profile_binding_list *grown = realloc(profile->default_lists,
            (profile->default_list_count + 1) * sizeof *grown);
        if (grown == NULL) qa_range_error("Out of memory");
        profile->default_lists = grown;
        profile->default_lists[profile->default_list_count++] =
            (profile_binding_list){profile->seat_defaults, profile->seat_default_count};
      }
      profile->seat_defaults = NULL;
      profile->seat_default_count = 0;
      profile->seat_started = false;
      profile->index++;
      return false;
    }
  }
  if (profile->stage == 2) {
    if (!profile->late_appended) {
      profile->late_appended = true;
      if (profile->has_phases && profile->dialect != QA_COMMAND_DIALECT_Q1_NETQUAKE
          && profile->dialect != QA_COMMAND_DIALECT_Q1_QUAKEWORLD)
        qa_command_buffer_append_preparation(profile_commands(profile), profile->phases.late, &launch, profile->dialect);
    }
    if (profile->pending_advance) {
      profile->pending_advance = false;
      qa_command_buffer_advance_program_frame(profile_commands(profile));
    }
    if (qa_command_buffer_preparation_complete(profile_commands(profile))) {
      profile->stage = 3;
      return true;
    }
    profile->pending_advance = true;
    return false;
  }
  return true;
}
static bool profile_prefix_run(void *ctx) {
  qa_app_prepared_profile *profile = ctx;
  for (;;) {
    if (profile_run_step(profile)) return true;
    if (profile->stopped || profile->args->client_source != NULL) return false;
    if (profile->args->next_frame) profile->args->next_frame(profile->args->next_frame_ctx);
  }
}
static qa_app_startup_config *profile_continuation_active(void *ctx) {
  return ((qa_app_prepared_profile *)ctx)->active;
}
static bool profile_continuation_advance(void *ctx, qa_app_prepared_startup *owner) {
  qa_app_prepared_profile *profile = ctx;
  if (owner != profile->published) qa_range_error("Configuration continuation belongs to another client");
  return profile_run_step(profile);
}
static void profile_register_named(qa_app_prepared_profile *profile, const char *name) {
  if (profile->named_count == profile->named_capacity) {
    size_t capacity = profile->named_capacity == 0 ? 16 : profile->named_capacity * 2;
    profile_named **grown = realloc(profile->named, capacity * sizeof *grown);
    if (!grown) qa_range_error("Out of memory");
    profile->named = grown;
    profile->named_capacity = capacity;
  }
  /* Individually allocated: the buffer holds the ctx pointer. */
  profile_named *named = profile_xmalloc(sizeof *named);
  named->profile = profile;
  named->name = name;
  profile->named[profile->named_count++] = named;
  qa_command_buffer *commands = qa_app_prepared_client_commands_buffer(profile->program);
  if (!qa_command_buffer_register(commands, name, profile_named_handler, named, NULL, QA_COMMAND_WORLD))
    qa_range_error("Out of memory");
}
qa_app_prepared_profile *qa_app_prepare_profile_configuration(qa_app_options *options_in,
    const qa_app_prepare_profile_args *args) {
  qa_app_prepared_profile *profile = profile_xcalloc(1, sizeof *profile);
  profile->args = args;
  profile->seat_count = args->seat_count;
  qa_app_prepared_startup *prepared = args->prepared;
  const qa_app_profile_client_source *client = args->client_source;
  qa_app_options options = *options_in;
  qa_q3_application_product content_q3;
  if (qa_app_configuration_content_q3_value(args->content, &content_q3)) {
    options.has_q3_product = true;
    options.q3_product = content_q3;
  }
  profile->options = options;
  profile->selection = qa_app_configuration_content_selection(args->content);
  profile->dialect = qa_app_configuration_dialect_in_catalog(args->catalog, args->content);
  size_t timing_count = 0;
  const qa_provider_timing *timing = qa_app_configuration_content_timing(args->content, &timing_count);
  profile->movement_dialect = timing_count > 0
      ? qa_app_configuration_movement_dialect_for(timing, timing_count, profile->selection.movement.provider)
      : qa_app_configuration_movement_dialect(&profile->selection);
  qa_command_dialect dialect = profile->dialect;
  qa_command_dialect movement_dialect = profile->movement_dialect;
  profile->has_phases = client != NULL && client->has_startup_commands;
  if (profile->has_phases)
    profile->phases = qa_app_startup_command_phases(client->startup_commands,
        client->startup_command_count, dialect);
  qa_cvar_registry *prepared_source = qa_app_prepared_startup_source(prepared);
  qa_cvar_registry *prepared_movement = qa_app_prepared_startup_movement(prepared);
  qa_command_buffer *prepared_commands = qa_app_prepared_startup_commands(prepared);
  if (client != NULL && client->cvars != NULL) {
    profile->source = client->cvars;
  } else {
    qa_app_application_source_selection source_selection =
        {profile->selection.source, profile->selection.match, NULL};
    profile->source = qa_app_create_startup_source(&options, source_selection, dialect,
        qa_cvar_registry_context(prepared_source), args->default_capacity, args->print, args->print_ctx);
    if (profile->source == NULL) qa_range_error("Out of memory");
    profile->owns_source = true;
  }
  qa_app_register_gtv_cvars(profile->source);
  if (qa_cvar_registry_dialect(profile->source) != dialect)
    qa_range_error("Configuration source dialect differs from its selected product");
  qa_cvar_registry_options movement_options = {0};
  movement_options.dialect = movement_dialect;
  movement_options.context = qa_cvar_registry_context(prepared_movement);
  movement_options.print = args->print;
  movement_options.print_ctx = args->print_ctx;
  profile->movement = qa_cvar_registry_create(&movement_options);
  if (profile->movement == NULL) qa_range_error("Out of memory");
  if (client != NULL && client->cvars != NULL) {
    profile->fallback = client->cvars;
  } else if (movement_dialect == dialect) {
    profile->fallback = profile->movement;
  } else {
    qa_cvar_registry_options fallback_options = {0};
    fallback_options.dialect = dialect;
    fallback_options.context = qa_command_buffer_context(prepared_commands);
    fallback_options.print = args->print;
    fallback_options.print_ctx = args->print_ctx;
    profile->fallback = qa_cvar_registry_create(&fallback_options);
    if (profile->fallback == NULL) qa_range_error("Out of memory");
    profile->owns_fallback = true;
  }
  profile->seats = profile_xcalloc(profile->seat_count + 1, sizeof *profile->seats);
  profile->inputs = profile_xcalloc(profile->seat_count + 1, sizeof *profile->inputs);
  profile->owned_seat_cvars = profile_xcalloc(profile->seat_count + 1, sizeof *profile->owned_seat_cvars);
  profile->mouse_cvars = profile_xcalloc(profile->seat_count + 1, sizeof *profile->mouse_cvars);
  profile->profiles = profile_xcalloc(profile->seat_count + 1, sizeof *profile->profiles);
  profile->has_profile = profile_xcalloc(profile->seat_count + 1, sizeof *profile->has_profile);
  qa_cvar_archive_entry **archives = profile_xcalloc(profile->seat_count + 1, sizeof *archives);
  size_t *archive_counts = profile_xcalloc(profile->seat_count + 1, sizeof *archive_counts);
  qa_cvar_archive_entry **mouse_archives = profile_xcalloc(profile->seat_count + 1, sizeof *mouse_archives);
  size_t *mouse_counts = profile_xcalloc(profile->seat_count + 1, sizeof *mouse_counts);
  qa_config_store client_store = {NULL};
  bool has_client_store = profile->seat_count > 0 && qa_app_configuration_store_for_catalog(
      args->catalog, &options, profile->selection.engine_behavior.content, &client_store);
  const char *character = qa_game_family_name(options.character);
  for (size_t index = 0; index < profile->seat_count; index++) {
    qa_session_seat *seat = args->seats[index].seat;
    qa_seat_input *input = args->seats[index].input;
    qa_seat_id seat_id = qa_session_seat_id(seat);
    qa_client_id client_id = qa_session_client_id(qa_session_seat_client(seat));
    if (!qa_same_seat(qa_seat_input_seat(input), seat_id))
      qa_range_error("Configuration input belongs to another seat");
    qa_command_context context = {0};
    context.session = seat_id.session;
    context.origin.kind = QA_COMMAND_ORIGIN_LOCAL_SEAT;
    context.origin.seat = seat_id;
    context.origin.client = client_id;
    qa_cvar_registry *cvars = NULL;
    if (client != NULL && client->seat_cvars != NULL) cvars = client->seat_cvars[index];
    if (cvars == NULL && client != NULL) cvars = client->cvars;
    if (cvars == NULL) {
      qa_cvar_registry_options cvar_options = {0};
      cvar_options.dialect = dialect;
      cvar_options.context = context;
      cvar_options.print = args->print;
      cvar_options.print_ctx = args->print_ctx;
      cvar_options.cheats_allowed = cheats_allowed;
      cvar_options.cheats_allowed_ctx = profile->source;
      cvars = qa_cvar_registry_create(&cvar_options);
      if (cvars == NULL) qa_range_error("Out of memory");
      profile->owned_seat_cvars[index] = cvars;
    }
    if (client != NULL) {
      qa_command_context cvar_context = qa_cvar_registry_context(cvars);
      if (qa_cvar_registry_dialect(cvars) != dialect
          || cvar_context.origin.kind != QA_COMMAND_ORIGIN_LOCAL_SEAT
          || !qa_same_seat(cvar_context.origin.seat, seat_id)
          || !qa_same_client(cvar_context.origin.client, client_id))
        qa_range_error("Client configuration requires its actual primary seat cvar owner");
    }
    if (client == NULL) {
      qa_app_register_player_userinfo(cvars, (double)seat_id.index,
          options.character == QA_GAME_FAMILY_Q2 ? options.character_model : "male");
      qa_app_character_appearance appearance = {character, options.character_model, options.character_skin,
          options.has_character_top_color, options.character_top_color,
          options.has_character_bottom_color, options.character_bottom_color};
      qa_app_apply_character_appearance(cvars, appearance);
      if (dialect == QA_COMMAND_DIALECT_Q3) {
        char player_name[64];
        (void)snprintf(player_name, sizeof player_name, "Player %lld", (long long)(seat_id.index + 1));
        qa_app_q3_client_identity identity = {player_name, qa_app_q3_client_model(character, options.character_model)};
        qa_app_initialize_q3_client_cvars(cvars, identity);
      }
    }
    qa_cvar_registry_options mouse_options = {0};
    mouse_options.dialect = dialect;
    mouse_options.context = context;
    mouse_options.print = args->print;
    mouse_options.print_ctx = args->print_ctx;
    qa_cvar_registry *mouse_cvars = qa_cvar_registry_create(&mouse_options);
    if (mouse_cvars == NULL) qa_range_error("Out of memory");
    profile->mouse_cvars[index] = mouse_cvars;
    qa_mouse_settings mouse = {NULL};
    qa_mouse_settings_init(&mouse, mouse_cvars);
    qa_app_register_run_cvar(mouse_cvars, movement_dialect);
    char *profile_name = qa_app_configuration_seat_profile_name((size_t)seat_id.index);
    profile->has_profile[index] = qa_config_store_load_seat(args->settings, profile_name, &profile->profiles[index]);
    free(profile_name);
    if (has_client_store)
      archives[index] = qa_app_configuration_load_client_archive(&client_store,
          profile->selection.engine_behavior.content, profile->selection.engine_behavior.provider,
          (size_t)seat_id.index, dialect, &archive_counts[index]);
    mouse_archives[index] = qa_app_configuration_load_input_archive(args->settings,
        (size_t)seat_id.index, dialect, &mouse_counts[index]);
    profile->seats[index].context = context;
    profile->seats[index].id = seat_id;
    profile->seats[index].cvars = cvars;
    profile->seats[index].mouse = mouse;
    profile->seats[index].profile = profile->has_profile[index] ? &profile->profiles[index] : NULL;
    profile->seats[index].archive = archives[index];
    profile->seats[index].archive_count = archive_counts[index];
    profile->seats[index].mouse_archive = mouse_archives[index];
    profile->seats[index].mouse_archive_count = mouse_counts[index];
    profile->inputs[index] = input;
  }
  if (has_client_store) qa_config_store_destroy(&client_store);
  /* The archive arrays ride the seat rows; keep the index arrays for
   * destroy by stashing them behind the profile (freed there). */
  profile->movement_archive = qa_app_configuration_load_movement_archive(args->settings,
      movement_dialect, &profile->movement_archive_count);
  profile->fallback_archive = qa_app_configuration_load_fallback_archive(args->settings,
      dialect, &profile->fallback_archive_count);
  {
    size_t shared_count = 0, device_count = 0;
    qa_cvar_archive_entry *shared_entries = qa_cvar_registry_archive_entries(args->shared, NULL, NULL, &shared_count);
    qa_cvar_archive_entry *device_entries = NULL;
    if (client != NULL && client->fresh_input) {
      qa_config_store device_store = {NULL};
      if (qa_app_input_device_store(options.user_content_root, &device_store)) {
        device_entries = qa_app_load_input_device_settings(&device_store, &device_count);
        qa_config_store_destroy(&device_store);
      }
    }
    profile->shared_archive = profile_xcalloc(shared_count + device_count + 1, sizeof *profile->shared_archive);
    for (size_t i = 0; i < shared_count; i++)
      profile->shared_archive[profile->shared_archive_count++] =
          (qa_cvar_archive_entry){profile_dup(shared_entries[i].name), profile_dup(shared_entries[i].value)};
    for (size_t i = 0; i < device_count; i++)
      profile->shared_archive[profile->shared_archive_count++] =
          (qa_cvar_archive_entry){profile_dup(device_entries[i].name), profile_dup(device_entries[i].value)};
    qa_app_cvar_archive_entries_free(shared_entries, shared_count);
    qa_app_cvar_archive_entries_free(device_entries, device_count);
  }
  qa_app_console_routing_options routing_options = {0};
  routing_options.ctx = profile;
  routing_options.fallback = profile->fallback;
  routing_options.source_dialect = profile_routing_dialect;
  routing_options.server = profile_routing_server;
  routing_options.seat = profile_routing_seat;
  routing_options.input = profile_routing_input;
  routing_options.movement = profile_routing_movement;
  routing_options.shared = profile_routing_shared;
  profile->routing = qa_app_console_routing_create(routing_options);
  if (profile->routing == NULL) qa_range_error("Out of memory");
  qa_app_configuration_script_reader(args->catalog, &profile->selection, &options,
      args->scripts, &profile->read, &profile->read_ctx);
  /* Binding choices + retained configurations (TS closure state). */
  profile->choices = profile_xcalloc(profile->seat_count + 1, sizeof *profile->choices);
  profile->retained = profile_xcalloc(profile->seat_count + 1, sizeof *profile->retained);
  size_t prior_count = 0;
  const qa_app_prepared_seat *priors = qa_app_prepared_startup_seats(prepared, &prior_count);
  for (size_t index = 0; index < profile->seat_count; index++) {
    profile_choice *choice = &profile->choices[index];
    choice->id = profile->seats[index].id;
    if (args->seats[index].has_selected) {
      choice->selected = args->seats[index].selected_bindings;
      choice->selected_count = args->seats[index].selected_count;
    } else {
      choice->owned_selected = qa_default_bindings(0, movement_dialect, NULL, 0, &choice->selected_count);
      if (choice->owned_selected == NULL) qa_range_error("Out of memory");
      choice->owned_selected_count = choice->selected_count;
      choice->selected = choice->owned_selected;
    }
    if (client != NULL && client->fresh_input) continue;
    const qa_app_prepared_seat *prior = NULL;
    for (size_t i = 0; i < prior_count; i++)
      if (qa_same_seat(priors[i].id, choice->id) && priors[i].input == profile->inputs[index]) prior = &priors[i];
    if (prior == NULL) continue;
    profile_retained *retained = &profile->retained[index];
    retained->present = true;
    size_t binding_count = 0;
    const qa_input_binding *bindings = qa_seat_input_bindings(prior->input, &binding_count);
    retained->bindings = profile_copy_bindings(bindings, binding_count);
    retained->binding_count = binding_count;
    retained->mouse = qa_mouse_settings_read(&prior->mouse);
    qa_cvar_read run_read = {0};
    if (qa_cvar_registry_find(prior->mouse.cvars, "cl_run", &run_read)) {
      retained->has_run = true;
      retained->run = profile_dup(run_read.value);
      qa_cvar_read_free(&run_read);
    }
    bool has_selected = false;
    char *const *overridden = NULL;
    const qa_input_binding *selected = NULL;
    (void)qa_app_prepared_startup_seat_choices(prepared, prior->id, &retained->all,
        &overridden, &retained->overridden_count, &has_selected, &selected,
        &retained->selected_count, NULL, NULL);
    retained->overridden = profile_xcalloc(retained->overridden_count + 1, sizeof *retained->overridden);
    for (size_t i = 0; i < retained->overridden_count; i++)
      retained->overridden[i] = profile_dup(overridden[i]);
    if (has_selected) {
      retained->selected = selected;
    } else {
      retained->owned_selected = qa_default_bindings(0, qa_cvar_registry_dialect(prepared_movement),
          NULL, 0, &retained->selected_count);
      if (retained->owned_selected == NULL) qa_range_error("Out of memory");
      retained->owned_selected_count = retained->selected_count;
      retained->selected = retained->owned_selected;
    }
  }
  /* prepareClientCommands over the candidate program. */
  qa_command_cvar_routing routing_view = qa_app_console_routing_view(profile->routing);
  if (client != NULL && client->route != NULL)
    routing_view = client->route(client->route_ctx, routing_view);
  qa_command_buffer_options program_options = {0};
  if (profile->has_phases) program_options.startup_command_text = profile->phases.stuffed;
  program_options.dialect = dialect;
  program_options.context = qa_command_buffer_context(prepared_commands);
  program_options.cvars = profile->fallback;
  program_options.cvar_routing = routing_view;
  program_options.read_script = profile_read_script;
  program_options.read_script_ctx = profile;
  program_options.on_script_complete = profile_script_complete;
  program_options.on_script_complete_ctx = profile;
  program_options.print = profile_print_source;
  program_options.print_ctx = profile;
  program_options.allow_command = profile_allow_command;
  program_options.allow_command_ctx = profile;
  program_options.forward_to_server = profile_forward_to_server;
  program_options.forward_to_server_ctx = profile;
  qa_app_prepared_seat *program_inputs = profile_xcalloc(profile->seat_count + 1, sizeof *program_inputs);
  for (size_t i = 0; i < profile->seat_count; i++) {
    program_inputs[i].context = profile->seats[i].context;
    program_inputs[i].id = profile->seats[i].id;
    program_inputs[i].cvars = profile->seats[i].cvars;
    program_inputs[i].mouse = profile->seats[i].mouse;
    program_inputs[i].input = profile->inputs[i];
  }
  profile->program = qa_app_prepared_startup_prepare_client_commands(prepared, &program_options,
      program_inputs, profile->seat_count, true);
  free(program_inputs);
  if (profile->program == NULL) qa_range_error("Out of memory");
  if (dialect == QA_COMMAND_DIALECT_Q3) {
    qa_q3_product_policy policy = options.has_q3_product ? options.q3_product.policy
        : (qa_q3_product_policy){QA_Q3_PRODUCT_RETAIL, QA_Q3_TEAM_ARENA_UI_RETAIL};
    size_t map_count = 0;
    const char *const *maps = qa_q3_product_map_commands(policy, &map_count);
    for (size_t i = 0; i < map_count; i++) profile_register_named(profile, maps[i]);
  } else {
    profile_register_named(profile, "map");
  }
  size_t routed_count = 0;
  const char *const *routed = qa_app_configuration_routed_commands(&routed_count);
  for (size_t i = 0; i < routed_count; i++) profile_register_named(profile, routed[i]);
  if (dialect == QA_COMMAND_DIALECT_Q1_NETQUAKE || dialect == QA_COMMAND_DIALECT_Q1_QUAKEWORLD)
    profile_register_named(profile, "timedemo");
  qa_command_buffer *program_commands = qa_app_prepared_client_commands_buffer(profile->program);
  profile->q1_registration = qa_app_register_q1_client_commands(program_commands, dialect,
      profile_execute_client, profile);
  profile->q2_registration = qa_app_register_q2_client_commands(program_commands, dialect,
      profile_execute_client, profile);
  profile->product = qa_content_catalog_product(args->catalog, profile->selection.engine_behavior.content);
  profile->base = profile->product->expectation.base_product == NULL ? profile->product
      : qa_content_catalog_product(args->catalog, profile->product->expectation.base_product);
  profile->complete = qa_app_prepared_client_commands_prepare_prefix(profile->program,
      profile_prefix_run, profile);
  qa_app_team_arena_launch_overrides *launch = NULL;
  if (client == NULL && options.team_arena_skirmish != NULL) {
    launch = qa_app_team_arena_launch_overrides_create(options.team_arena_skirmish);
    if (launch == NULL) qa_range_error("Out of memory");
    qa_app_team_arena_launch_seat *launch_seats =
        profile_xcalloc(profile->seat_count + 1, sizeof *launch_seats);
    for (size_t i = 0; i < profile->seat_count; i++)
      launch_seats[i] = (qa_app_team_arena_launch_seat){profile->seats[i].id, profile->seats[i].cvars};
    qa_app_team_arena_launch_overrides_apply(launch, profile->source, launch_seats, profile->seat_count);
    free(launch_seats);
  }
  const qa_app_q3_map_launch *q3_launch = options.q3_map_launch;
  if (q3_launch != NULL) qa_app_apply_q3_map_launch(profile->source, q3_launch, "all");
  if (client == NULL) {
    double capacity = args->default_capacity;
    if (options.team_arena_skirmish != NULL)
      capacity = ((const qa_app_team_arena_skirmish *)options.team_arena_skirmish)->max_clients;
    if (q3_launch != NULL && q3_launch->has_max_clients) capacity = q3_launch->max_clients;
    size_t definition_count = 0;
    const qa_server_setting_definition **definitions = qa_server_definitions_for_selection(
        profile->selection.source, profile->selection.match, profile->selection.combat, &definition_count);
    qa_app_startup_rules resolved = qa_app_resolve_startup_rules(profile->options, profile->source,
        capacity, definitions, definition_count, launch == NULL && q3_launch == NULL);
    free(definitions);
    profile->options = resolved.options;
    profile->max_clients = resolved.max_clients;
  } else {
    profile->max_clients = args->default_capacity;
  }
  if (launch != NULL) qa_app_team_arena_launch_overrides_destroy(launch);
  /* Per-seat archive pointers ride the seat rows (freed at destroy). */
  free(archives);
  free(mouse_archives);
  free(archive_counts);
  free(mouse_counts);
  return profile;
}
qa_cvar_registry *qa_app_prepared_profile_source(qa_app_prepared_profile *profile) { return profile->source; }
qa_cvar_registry *qa_app_prepared_profile_movement(qa_app_prepared_profile *profile) { return profile->movement; }
qa_cvar_registry *qa_app_prepared_profile_fallback(qa_app_prepared_profile *profile) { return profile->fallback; }
const qa_app_prepared_seat_configuration *qa_app_prepared_profile_seats(
    qa_app_prepared_profile *profile, size_t *out_count) {
  *out_count = profile->seat_count;
  return profile->seats;
}
qa_seat_input *qa_app_prepared_profile_seat_input(qa_app_prepared_profile *profile, size_t index) {
  return profile->inputs[index];
}
qa_app_prepared_client_commands *qa_app_prepared_profile_program(qa_app_prepared_profile *profile) {
  return profile->program;
}
qa_app_console_routing *qa_app_prepared_profile_routing(qa_app_prepared_profile *profile) {
  return profile->routing;
}
qa_app_console_script_files *qa_app_prepared_profile_scripts(qa_app_prepared_profile *profile) {
  return profile->args->scripts;
}
qa_app_prepared_scoped_read_fn qa_app_prepared_profile_read(qa_app_prepared_profile *profile, void **out_ctx) {
  *out_ctx = profile->read_ctx;
  return profile->read;
}
const qa_app_options *qa_app_prepared_profile_options(const qa_app_prepared_profile *profile) {
  return &profile->options;
}
double qa_app_prepared_profile_max_clients(const qa_app_prepared_profile *profile) {
  return profile->max_clients;
}
const qa_app_configuration_command_request *qa_app_prepared_profile_requests(
    const qa_app_prepared_profile *profile, size_t *out_count) {
  *out_count = profile->request_count;
  return profile->requests;
}
size_t qa_app_prepared_profile_binding_choices(qa_app_prepared_profile *profile,
    qa_app_profile_binding_choice *out, size_t capacity) {
  size_t count = profile->seat_count < capacity ? profile->seat_count : capacity;
  for (size_t i = 0; i < count; i++) {
    profile_choice *choice = &profile->choices[i];
    out[i].id = choice->id;
    out[i].overridden_keys = choice->overridden;
    out[i].overridden_count = choice->overridden_count;
    out[i].all_bindings_chosen = choice->all;
    out[i].selected_bindings = choice->selected;
    out[i].selected_count = choice->selected_count;
    out[i].authored_bindings = choice->has_authored ? choice->authored : NULL;
    out[i].authored_count = choice->has_authored ? choice->authored_count : 0;
  }
  return profile->seat_count;
}
void qa_app_prepared_profile_publish_continuation(qa_app_prepared_profile *profile,
    qa_app_prepared_startup *owner) {
  if (owner != profile->args->prepared) qa_range_error("Configuration continuation belongs to another client");
  if (profile->published != NULL) qa_range_error("Configuration continuation already published");
  profile->published = owner;
  if (!profile->complete) {
    qa_app_prepared_configuration_continuation continuation = {0};
    continuation.ctx = profile;
    continuation.active = profile_continuation_active;
    continuation.advance = profile_continuation_advance;
    qa_app_prepared_startup_adopt_configuration_continuation(owner, continuation);
  }
}
void qa_app_prepared_profile_apply_binding_defaults(qa_app_prepared_profile *profile, qa_seat_id seat,
    const qa_input_binding *defaults, size_t default_count) {
  profile_apply_defaults(profile, seat, defaults, default_count);
}
void qa_app_prepared_profile_forward_commands(qa_app_prepared_profile *profile,
    qa_app_profile_forward_fn forward, void *forward_ctx) {
  profile->forward = forward;
  profile->forward_ctx = forward_ctx;
}
void qa_app_prepared_profile_destroy(qa_app_prepared_profile *profile) {
  if (profile == NULL) return;
  if (profile->q1_registration) qa_app_q1_client_commands_release(profile->q1_registration);
  if (profile->q2_registration) qa_app_q2_client_commands_release(profile->q2_registration);
  if (profile->active) qa_app_startup_config_destroy(profile->active);
  if (profile->program) qa_app_prepared_client_commands_destroy(profile->program);
  if (profile->routing) qa_app_console_routing_destroy(profile->routing);
  for (size_t i = 0; i < profile->seat_count; i++) {
    if (profile->owned_seat_cvars && profile->owned_seat_cvars[i])
      qa_cvar_registry_destroy(profile->owned_seat_cvars[i]);
    if (profile->mouse_cvars && profile->mouse_cvars[i]) qa_cvar_registry_destroy(profile->mouse_cvars[i]);
    if (profile->has_profile && profile->has_profile[i]) qa_seat_settings_free(&profile->profiles[i]);
    if (profile->seats) {
      qa_app_cvar_archive_entries_free((qa_cvar_archive_entry *)profile->seats[i].archive,
          profile->seats[i].archive_count);
      qa_app_cvar_archive_entries_free((qa_cvar_archive_entry *)profile->seats[i].mouse_archive,
          profile->seats[i].mouse_archive_count);
    }
    if (profile->choices) {
      free(profile->choices[i].authored);
      profile_bindings_dispose(profile->choices[i].owned_selected, profile->choices[i].owned_selected_count);
      for (size_t j = 0; j < profile->choices[i].overridden_count; j++) free(profile->choices[i].overridden[j]);
      free(profile->choices[i].overridden);
    }
    if (profile->retained) {
      free(profile->retained[i].bindings);
      free(profile->retained[i].run);
      profile_bindings_dispose(profile->retained[i].owned_selected, profile->retained[i].owned_selected_count);
      for (size_t j = 0; j < profile->retained[i].overridden_count; j++) free(profile->retained[i].overridden[j]);
      free(profile->retained[i].overridden);
    }
  }
  if (profile->owns_source && profile->source) qa_cvar_registry_destroy(profile->source);
  if (profile->movement) qa_cvar_registry_destroy(profile->movement);
  if (profile->owns_fallback && profile->fallback) qa_cvar_registry_destroy(profile->fallback);
  if (profile->has_phases) qa_app_startup_phases_free(&profile->phases);
  qa_app_cvar_archive_entries_free(profile->movement_archive, profile->movement_archive_count);
  qa_app_cvar_archive_entries_free(profile->fallback_archive, profile->fallback_archive_count);
  for (size_t i = 0; i < profile->shared_archive_count; i++) {
    free(profile->shared_archive[i].name);
    free(profile->shared_archive[i].value);
  }
  free(profile->shared_archive);
  for (size_t i = 0; i < profile->request_count; i++) {
    free((void *)profile->requests[i].name);
    for (size_t j = 0; j < profile->requests[i].argument_count; j++)
      free((void *)profile->requests[i].arguments[j]);
    free((void *)profile->requests[i].arguments);
    qa_command_context_release(&profile->requests[i].source);
  }
  free(profile->requests);
  for (size_t i = 0; i < profile->server_name_count; i++) free(profile->server_names[i]);
  free(profile->server_names);
  for (size_t i = 0; i < profile->named_count; i++) free(profile->named[i]);
  free(profile->named);
  free(profile->seats);
  free(profile->inputs);
  free(profile->owned_seat_cvars);
  free(profile->mouse_cvars);
  free(profile->profiles);
  free(profile->has_profile);
  free(profile->choices);
  free(profile->retained);
  for (size_t i = 0; i < profile->default_list_count; i++)
    profile_bindings_dispose(profile->default_lists[i].bindings, profile->default_lists[i].count);
  free(profile->default_lists);
  if (profile->seat_defaults_owned) profile_bindings_dispose(profile->seat_defaults, profile->seat_default_count);
  qa_app_configuration_script_reader_free(profile->read_ctx);
  free(profile);
}
