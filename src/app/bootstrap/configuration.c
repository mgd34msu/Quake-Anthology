/* Port of src/app/bootstrap/configuration.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/app/bootstrap/configuration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/audio/output_settings.h"
#include "qa/app/bootstrap/audio_settings.h"
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/app/bootstrap/gtv_commands.h"
#include "qa/app/bootstrap/input_devices.h"
#include "qa/app/bootstrap/player_userinfo.h"
#include "qa/app/bootstrap/q3_client/userinfo.h"
#include "qa/app/bootstrap/q3_map_command.h"
#include "qa/app/bootstrap/team_arena_skirmish.h"
#include "qa/app/bootstrap/view_settings.h"
#include "qa/content/catalog/source_program.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
#include "qa/input/support.h"
#include "qa/settings/server/selection.h"
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
  initial->source = qa_app_create_startup_source(args->source_host, &options, source_selection, dialect,
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
  bool has_audio = qa_app_load_audio_settings(args->settings, &audio);
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
  initial->image = qa_app_image_settings_open(image_options, args->image_host);
  /* sharedArchive: image persistedEntries have no landed accessor
   * (image_settings.h); input-device entries follow, as in TS. */
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
    if (has_volume && has_audio) {
      qa_app_write_audio_output_cvars(image_cvars, audio.output_format);
      for (size_t i = 0; i < qa_app_audio_output_cvar_name_count; i++) {
        char *value = qa_cvar_registry_variable_string(image_cvars, qa_app_audio_output_cvar_names[i]);
        shared_push(initial, qa_app_audio_output_cvar_names[i], value != NULL ? value : "");
        free(value);
      }
      shared_push_number(initial, "volume", audio.effects_volume);
      shared_push_number(initial, "bgmvolume", audio.music_volume);
      if (audio.has_music_shuffle) shared_push(initial, "music_shuffle", audio.music_shuffle ? "1" : "0");
      if (audio.has_menu_track) shared_push(initial, "music_menu_track", audio.menu_track);
    }
    qa_app_view_settings *view = qa_app_view_settings_create(view_changed, image_cvars);
    if (view == NULL) qa_range_error("Out of memory");
    qa_app_view_settings_load(view, args->settings);
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
