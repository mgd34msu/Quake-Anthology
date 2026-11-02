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
#include "qa/app/bootstrap/q3_product.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/time.h"
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
/* --- Catalog-backed forms (landed qa_content_catalog services; the
 * host forms above stay for existing callers). --- */
/* TS CommandDialect exact text for a dialect. */
const char *qa_app_configuration_dialect_name(qa_command_dialect dialect);
/* TS configurationDialect over a discovered catalog product. */
qa_command_dialect qa_app_configuration_dialect_for_product(const qa_catalog_discovered_product *product);
qa_command_dialect qa_app_configuration_dialect_in_catalog(const qa_content_catalog *catalog,
                                                           const qa_app_configuration_content *content);
/* TS configurationMovementDialect: the timing whose provider equals
 * the movement provider selects the dialect (its clock kind); a
 * missing timing aborts with the exact TS Error. */
qa_command_dialect qa_app_configuration_movement_dialect_for(const qa_provider_timing *timing, size_t timing_count,
                                                             qa_provider_id movement_provider);
qa_command_dialect qa_app_configuration_movement_dialect(const qa_app_application_configuration_selection *selection);
/* TS configurationStore / legacyConfigurationOptions over a catalog. */
bool qa_app_configuration_store_for_catalog(const qa_content_catalog *catalog, const qa_app_options *options,
                                            const char *reference, qa_config_store *out);
bool qa_app_legacy_configuration_options_for_catalog(const qa_content_catalog *catalog, const qa_app_options *options,
                                                     const char *reference, qa_app_legacy_console_config_sources *out,
                                                     const char **roots_storage, size_t capacity, char **owned_storage);
/* TS configurationScriptReader roots + the hasMod test: caller arrays
 * borrow catalog strings. has_mod mirrors
 * product.contentDirectory !== base.contentDirectory. */
typedef struct qa_app_configuration_script_roots {
  const qa_catalog_discovered_product *product, *base;
  const char *base_roots[2]; size_t base_root_count;
  const char *game_roots[2]; size_t game_root_count;
  bool has_mod;
} qa_app_configuration_script_roots;
qa_app_configuration_script_roots qa_app_configuration_script_roots_for(const qa_content_catalog *catalog,
                                                                        const qa_app_application_configuration_selection *selection);
/* Malloc'd TS seat profile name: input/seat-<index+1>.json. */
char *qa_app_configuration_seat_profile_name(size_t seat_index);
/* TS loadCvarArchive owner tuples used by prepareInitial/Profile.
 * Malloc'd entries; free with qa_app_cvar_archive_entries_free. */
qa_cvar_archive_entry *qa_app_configuration_load_client_archive(const qa_config_store *store, const char *content,
                                                                const char *provider, size_t seat_index,
                                                                qa_command_dialect dialect, size_t *out_count);
qa_cvar_archive_entry *qa_app_configuration_load_input_archive(const qa_config_store *store, size_t seat_index,
                                                               qa_command_dialect dialect, size_t *out_count);
qa_cvar_archive_entry *qa_app_configuration_load_movement_archive(const qa_config_store *store,
                                                                  qa_command_dialect movement, size_t *out_count);
qa_cvar_archive_entry *qa_app_configuration_load_fallback_archive(const qa_config_store *store,
                                                                  qa_command_dialect dialect, size_t *out_count);
/* TS dispatch/forward request construction: unwinds script origins
 * to find the originating local seat (has_seat false mirrors null). */
qa_app_configuration_command_request qa_app_configuration_make_request(const char *name, const char *const *arguments,
                                                                       size_t argument_count, qa_command_context source);
/* TS openInitialConfigurationContent over an already discovered
 * catalog: prepares the Q3 product (writing options->q3_product when
 * present), then opens recipe or launch-preset configuration content
 * and records the (possibly restricted) catalog on it. q3_host may be
 * NULL only when the selected product is not Q3. */
qa_app_configuration_content *qa_app_open_initial_configuration_content_for_catalog(qa_app_content_host content_host,
    const qa_content_catalog *catalog, qa_app_options *options, const qa_executable_recipe *recipe, bool has_recipe,
    const qa_app_q3_product_host *q3_host);
#endif
