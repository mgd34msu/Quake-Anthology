/* Port of src/app/bootstrap/configuration.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/configuration.h"
#include <stdlib.h>
#include <string.h>
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
