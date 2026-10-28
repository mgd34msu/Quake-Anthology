/* Port of src/app/bootstrap/configuration.ts. The prepared-startup
 * composition (landed prepared_startup/startup_config owners) is
 * wired through the host; dialect/store/legacy selection rules and
 * the routed command-name table are kept here. Async -> error
 * strings. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CONFIGURATION_H
#define QA_APP_BOOTSTRAP_CONFIGURATION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/config_scripts.h"
#include "qa/app/bootstrap/content.h"
#include "qa/app/bootstrap/options.h"
/* TS ConfigurationCommandRequest. seat_index -1 mirrors TS null. */
typedef struct qa_app_configuration_command_request {
  const char *target; /* "application" */
  const char *name;
  const char *const *arguments; size_t argument_count;
  bool has_seat; qa_seat_id seat;
  qa_command_context source;
} qa_app_configuration_command_request;
/* Catalog product facts used by the pure selectors. */
typedef struct qa_app_configuration_catalog_host {
  void *ctx;
  /* Fills family/edition/content directory + optional user root. */
  bool (*product)(void *ctx, const char *content_id, qa_game_family *family, const char **edition,
                  const char **content_directory, const char **user_root, const char **base_product);
  /* All q1 non-quakeworld product content directories/user roots. */
  size_t (*q1_products)(void *ctx, const char **directories, const char **user_roots, size_t capacity);
  /* Content open / prepared composition steps. */
  const char *(*open_initial)(void *ctx, qa_app_configuration_content *content);
  const char *(*prepare_profile)(void *ctx, const void *args);
  const char *(*prepare_initial)(void *ctx, const void *args);
} qa_app_configuration_host;
/* TS configurationDialect from family/edition. */
qa_command_dialect qa_app_configuration_dialect_for(qa_game_family family, const char *edition);
qa_command_dialect qa_app_configuration_dialect(qa_app_configuration_host host, const qa_app_configuration_content *content);
/* TS configurationStore: fills a store for the reference product. */
bool qa_app_configuration_store(qa_app_configuration_host host, const qa_app_options *options,
                                const qa_app_configuration_content *content, const char *reference, qa_config_store *out);
/* TS legacyConfigurationOptions: false mirrors the TS {} result.
 * game_roots strings are malloc'd into caller arrays (capacity). */
bool qa_app_legacy_configuration_options(qa_app_configuration_host host, const qa_app_options *options, const char *reference,
                                         qa_app_legacy_console_config_sources *out, const char **roots_storage, size_t capacity, char **owned_storage);
/* Routed application command names registered in prepareProfileConfiguration
 * (static base list; q3 map commands added by the caller for q3). */
const char *const *qa_app_configuration_routed_commands(size_t *out_count);
qa_app_configuration_content *qa_app_open_initial_configuration_content(qa_app_configuration_host host, qa_app_content_host content_host,
                                                                        qa_app_options options, const qa_executable_recipe *recipe, bool has_recipe);
#endif
