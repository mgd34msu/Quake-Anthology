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
#include "qa/app/bootstrap/image_settings.h"
#include "qa/app/bootstrap/options.h"
#include "qa/app/bootstrap/prepared_startup.h"
#include "qa/app/bootstrap/q3_product.h"
#include "qa/app/bootstrap/startup_source.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/time.h"
#include "qa/world/session/session.h"
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
/* TS sourceConsoleScripts(content, options, settings): console scripts for a
 * loaded world, user files plus the scripts its source product mounts; the
 * content's main mounts stay retained (retainMainMounts) until the files close.
 * settings is borrowed for the files' life; destroy frees the adapters. */
qa_app_console_script_files *qa_app_source_console_scripts(qa_app_loaded_content *content, const qa_app_options *options,
    const qa_config_store *settings);
/* TS configurationScriptReader: fills a scoped reader over scripts,
 * the catalog product/base loose roots and the console seat root. */
void qa_app_configuration_script_reader(const qa_content_catalog *catalog,
    const qa_app_application_configuration_selection *selection, const qa_app_options *options,
    qa_app_console_script_files *scripts, qa_app_prepared_scoped_read_fn *out_read, void **out_ctx);
/* Frees a configurationScriptReader ctx (its options copy and root arrays); NULL is ignored. */
void qa_app_configuration_script_reader_free(void *ctx);
/* The ConsoleScriptFiles prepareInitialConfiguration builds over the configuration
 * content (TS configuration.ts): user scripts at the console root, the legacy config
 * roots and the scripts the content mounts. Closing the files closes the content
 * (TS () => content.close()); destroy frees the adapters. settings is borrowed. */
qa_app_console_script_files *qa_app_open_configuration_scripts(const qa_app_options *options,
    qa_app_configuration_content *content, const qa_config_store *settings);
/* TS prepareInitialConfiguration. The ConsoleScriptFiles owner is
 * caller-built over the content mounts (config-scripts.ts construction
 * sites own the mount adapters) and borrowed here. The result owns
 * the created registries, seat profiles/archives and requests;
 * release with qa_app_prepared_initial_destroy. EngineSession seats
 * stay session-owned. */
typedef struct qa_app_prepare_initial_args {
  const qa_content_catalog *catalog;
  qa_app_configuration_content *content;
  qa_engine_session *session;
  const qa_identity_owner *identity;
  const qa_config_store *settings;
  qa_app_console_script_files *scripts;
  qa_command_print_fn print; void *print_ctx;
  const qa_cvar_archive_entry *source_archive; size_t source_archive_count;
  double default_capacity;
  void (*next_frame)(void *ctx); void *next_frame_ctx;
  void (*on_prepared)(void *ctx, qa_app_prepared_startup *prepared); void *on_prepared_ctx;
} qa_app_prepare_initial_args;
typedef struct qa_app_prepared_initial qa_app_prepared_initial;
qa_app_prepared_initial *qa_app_prepare_initial_configuration(qa_app_options *options,
    const qa_app_prepare_initial_args *args);
qa_app_prepared_startup *qa_app_prepared_initial_startup(qa_app_prepared_initial *initial);
/* TS image (NULL mirrors the TS null ApplicationImageSettings). */
qa_app_image_settings *qa_app_prepared_initial_image(qa_app_prepared_initial *initial);
/* Resolved options: shallow TS object copy; do not destroy strings. */
const qa_app_options *qa_app_prepared_initial_options(const qa_app_prepared_initial *initial);
double qa_app_prepared_initial_max_clients(const qa_app_prepared_initial *initial);
/* Borrowed request rows (owned by the result). */
const qa_app_configuration_command_request *qa_app_prepared_initial_requests(
    const qa_app_prepared_initial *initial, size_t *out_count);
qa_app_console_script_files *qa_app_prepared_initial_scripts(qa_app_prepared_initial *initial);
/* TS localSeats map lookup; NULL when the client has no local seat. */
qa_session_seat *qa_app_prepared_initial_local_seat(qa_app_prepared_initial *initial, qa_client_id client);
void qa_app_prepared_initial_destroy(qa_app_prepared_initial *initial);
/* --- TS prepareProfileConfiguration. The ConsoleScriptFiles owner
 * is caller-built over the content mounts and borrowed, as in
 * prepareInitialConfiguration. client_source NULL mirrors the TS
 * undefined clientSource. --- */
/* TS clientSource. seat_cvars/seat_archives are parallel to the
 * profile seats (NULL entries mirror a missing Map entry). */
typedef struct qa_app_profile_client_source {
  bool fresh_input; /* inputState "fresh" (false = "retained") */
  bool has_startup_commands;
  const char *const *startup_commands; size_t startup_command_count;
  qa_cvar_registry *cvars;
  const char *archive_text; /* NULL mirrors TS null/undefined */
  qa_cvar_registry *const *seat_cvars;
  const qa_cvar_archive_entry *const *seat_archives;
  const size_t *seat_archive_counts; bool has_seat_archives;
  qa_command_cvar_routing (*route)(void *ctx, qa_command_cvar_routing base);
  void *route_ctx;
} qa_app_profile_client_source;
/* TS args.seats entries. */
typedef struct qa_app_profile_seat {
  qa_session_seat *seat;
  qa_seat_input *input;
  bool has_selected;
  const qa_input_binding *selected_bindings; size_t selected_count;
} qa_app_profile_seat;
typedef struct qa_app_prepare_profile_args {
  qa_app_prepared_startup *prepared;
  const qa_app_profile_client_source *client_source; /* NULL = undefined */
  const qa_app_profile_seat *seats; size_t seat_count;
  qa_app_configuration_content *content;
  const qa_content_catalog *catalog;
  const qa_config_store *settings;
  qa_cvar_registry *shared;
  qa_app_console_script_files *scripts;
  qa_command_print_fn print; void *print_ctx;
  const qa_cvar_archive_entry *source_archive; size_t source_archive_count;
  double default_capacity;
  void (*next_frame)(void *ctx); void *next_frame_ctx;
} qa_app_prepare_profile_args;
typedef struct qa_app_prepared_profile qa_app_prepared_profile;
/* TS PreparedProfileConfiguration.bindingChoices row. authored NULL
 * with count 0 mirrors TS null. Rows borrow profile storage. */
typedef struct qa_app_profile_binding_choice {
  qa_seat_id id;
  char *const *overridden_keys; size_t overridden_count;
  bool all_bindings_chosen;
  const qa_input_binding *selected_bindings; size_t selected_count;
  const qa_input_binding *authored_bindings; size_t authored_count;
} qa_app_profile_binding_choice;
/* TS forwardCommands handler. */
typedef void (*qa_app_profile_forward_fn)(void *ctx, const qa_app_configuration_command_request *request);
qa_app_prepared_profile *qa_app_prepare_profile_configuration(qa_app_options *options,
    const qa_app_prepare_profile_args *args);
qa_cvar_registry *qa_app_prepared_profile_source(qa_app_prepared_profile *profile);
qa_cvar_registry *qa_app_prepared_profile_movement(qa_app_prepared_profile *profile);
qa_cvar_registry *qa_app_prepared_profile_fallback(qa_app_prepared_profile *profile);
/* Borrowed TS seats: PreparedSeatConfiguration rows + their inputs. */
const qa_app_prepared_seat_configuration *qa_app_prepared_profile_seats(
    qa_app_prepared_profile *profile, size_t *out_count);
qa_seat_input *qa_app_prepared_profile_seat_input(qa_app_prepared_profile *profile, size_t index);
qa_app_prepared_client_commands *qa_app_prepared_profile_program(qa_app_prepared_profile *profile);
qa_app_console_routing *qa_app_prepared_profile_routing(qa_app_prepared_profile *profile);
qa_app_console_script_files *qa_app_prepared_profile_scripts(qa_app_prepared_profile *profile);
/* TS read (StartupConfigOptions["read"]). */
qa_app_prepared_scoped_read_fn qa_app_prepared_profile_read(qa_app_prepared_profile *profile, void **out_ctx);
const qa_app_options *qa_app_prepared_profile_options(const qa_app_prepared_profile *profile);
double qa_app_prepared_profile_max_clients(const qa_app_prepared_profile *profile);
const qa_app_configuration_command_request *qa_app_prepared_profile_requests(
    const qa_app_prepared_profile *profile, size_t *out_count);
size_t qa_app_prepared_profile_binding_choices(qa_app_prepared_profile *profile,
    qa_app_profile_binding_choice *out, size_t capacity);
/* TS publishContinuation / applyBindingDefaults / forwardCommands. */
void qa_app_prepared_profile_publish_continuation(qa_app_prepared_profile *profile,
    qa_app_prepared_startup *owner);
void qa_app_prepared_profile_apply_binding_defaults(qa_app_prepared_profile *profile, qa_seat_id seat,
    const qa_input_binding *defaults, size_t default_count);
void qa_app_prepared_profile_forward_commands(qa_app_prepared_profile *profile,
    qa_app_profile_forward_fn forward, void *forward_ctx);
void qa_app_prepared_profile_destroy(qa_app_prepared_profile *profile);
#endif
