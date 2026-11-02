/* Port of src/app/bootstrap/configuration.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/configuration.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/cvar_archives.h"
#include "qa/content/user_data.h"
#include "qa/foundation/support.h"
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
