/* Port of src/app/bootstrap/content.ts. The catalog/mount composition
 * (discover, preset selection, mount plans, map decode, mod/grapple/
 * weapon-behavior preparation) is wired by the host through the
 * callbacks below, as in landed startup.c; selection guards, the
 * discovers-mods predicate and opened-mounts bookkeeping are kept
 * here. Async -> synchronous. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_CONTENT_H
#define QA_APP_BOOTSTRAP_CONTENT_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/demo_playback.h"
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/content/catalog/launch.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/core/q3_product_policy.h"
/* TS ApplicationContentSource. */
typedef struct qa_app_content_source { const char *kind; qa_app_demo_family family; } qa_app_content_source;
/* TS ApplicationWorld = Q1Map | Q2DecodedMap | Q3WorldGeometry (opaque). */
typedef struct qa_app_application_world { const char *kind; const void *value; } qa_app_application_world;
typedef struct qa_app_application_map_sidecar {
  qa_content_id content; const char *path;
  const qa_resolved_resource_reference *resource; /* NULL = TS null */
} qa_app_application_map_sidecar;
/* TS ApplicationSourceSelection. Supersedes the startup_source.h
 * qa_app_startup_source_selection mirror of these fields. */
typedef struct qa_app_application_source_selection {
  qa_provider_reference source, match;
  const char *rules;
} qa_app_application_source_selection;
/* TS ApplicationConfigurationSelection. */
typedef struct qa_app_application_configuration_selection {
  qa_provider_reference source, engine_behavior, match, combat, movement;
  qa_provider_timing timing;
} qa_app_application_configuration_selection;
typedef struct qa_app_loaded_content qa_app_loaded_content;
typedef struct qa_app_configuration_content qa_app_configuration_content;
/* TS ApplicationConfigurationRequest union (kind text). */
typedef struct qa_app_configuration_request {
  const char *kind; /* "launch" | other host kinds */
  const qa_launch_preset *preset;
  qa_launch_choice choice;
} qa_app_configuration_request;
typedef struct qa_app_content_host {
  void *ctx;
  /* Catalog product facts used by the pure selectors. */
  bool (*product)(void *ctx, const char *id, qa_game_family *family, const char **edition, const char **campaign, bool *installed, bool *builtin);
  bool (*require_product)(void *ctx, const char *id); /* false aborts as catalog.require */
  /* Composition steps; return error text or NULL. */
  const char *(*open_loaded)(void *ctx, qa_app_loaded_content *content);
  const char *(*open_configuration)(void *ctx, qa_app_configuration_content *content);
  const char *(*open_remote)(void *ctx, qa_app_configuration_content *content);
  const char *(*for_content)(void *ctx, qa_app_loaded_content *content, qa_content_id content_id, const void **out_mounts);
  const char *(*prepare_mods)(void *ctx, qa_app_loaded_content *content, const char *purpose);
  const char *(*prepare_grapple)(void *ctx, qa_app_loaded_content *content);
  const char *(*prepare_weapon_behaviors)(void *ctx, qa_app_loaded_content *content);
  const char *(*close_loaded)(void *ctx, qa_app_loaded_content *content);
  const char *(*resolve_travel)(void *ctx, qa_app_loaded_content *content, const char *path, qa_executable_recipe *out);
  const char *(*preset)(void *ctx, const qa_app_options *options, bool configuration, qa_launch_preset *out);
} qa_app_content_host;
bool qa_app_application_discovers_mods(const qa_app_options *options, bool has_recipe,
                                       bool product_known, qa_game_family product_family);
qa_app_application_source_selection qa_app_application_source_selection_of(qa_app_content_host host, const char *product, const char *rules);
qa_app_loaded_content *qa_app_load_application_content(qa_app_content_host host, qa_app_options options,
                                                       const qa_executable_recipe *restored_recipe, bool has_recipe);
void qa_app_loaded_content_destroy(qa_app_loaded_content *content);
const qa_executable_recipe *qa_app_loaded_content_recipe(const qa_app_loaded_content *content);
void qa_app_loaded_content_set_recipe(qa_app_loaded_content *content, qa_executable_recipe recipe);
const void *qa_app_loaded_content_catalog(const qa_app_loaded_content *content);
void qa_app_loaded_content_set_catalog(qa_app_loaded_content *content, const void *catalog);
const void *qa_app_loaded_content_mounts(const qa_app_loaded_content *content);
void qa_app_loaded_content_set_mounts(qa_app_loaded_content *content, const void *mounts);
bool qa_app_loaded_content_q3_policy(const qa_app_loaded_content *content, qa_q3_product_policy *out);
void qa_app_loaded_content_set_q3_policy(qa_app_loaded_content *content, qa_q3_product_policy policy);
/* TS q3Product: false is TS null. */
bool qa_app_loaded_content_q3_product(const qa_app_loaded_content *content, qa_q3_application_product *out);
const void *qa_app_loaded_content_for_content(qa_app_loaded_content *content, qa_content_id content_id);
size_t qa_app_loaded_content_opened_mounts(qa_app_loaded_content *content, const void **out, size_t capacity);
void qa_app_loaded_content_prepare_mods(qa_app_loaded_content *content, const char *purpose);
/* TS preparedMods (native loader): the qa_prepared_mod pointers SimulationOptions.preparedMods
 * takes, owned by the content; never NULL (empty before prepareMods, as TS modOwners = []). */
const void *const *qa_app_loaded_content_prepared_mods(const qa_app_loaded_content *content, size_t *out_count);
void qa_app_loaded_content_prepare_qvm_grapple(qa_app_loaded_content *content);
void qa_app_loaded_content_prepare_weapon_behaviors(qa_app_loaded_content *content);
/* TS preparedWeaponBehaviors / preparedQvmGrapple of the concrete loader (owned by
 * the content; NULL grapple is TS null). On the concrete loader prepareWeaponBehaviors
 * and prepareQvmGrapple run prepareApplicationWeaponBehavior and
 * prepareApplicationQvmGrapple over forContent, raising Error as TS throws. */
struct qa_app_prepared_weapon_behavior;
struct qa_app_prepared_qvm_grapple;
const struct qa_app_prepared_weapon_behavior *qa_app_loaded_content_prepared_weapon_behaviors(const qa_app_loaded_content *content, size_t *out_count);
const struct qa_app_prepared_qvm_grapple *qa_app_loaded_content_prepared_qvm_grapple(const qa_app_loaded_content *content);
void qa_app_loaded_content_close(qa_app_loaded_content *content);
qa_executable_recipe qa_app_resolve_application_travel(qa_app_loaded_content *content, const char *path);
/* Configuration content (TS ApplicationConfigurationContent). */
qa_app_configuration_content *qa_app_open_application_configuration_content(qa_app_content_host host, qa_app_configuration_request request);
qa_app_configuration_content *qa_app_open_remote_application_content(qa_app_content_host host, qa_app_options options);
void qa_app_configuration_content_destroy(qa_app_configuration_content *content);
qa_app_application_configuration_selection qa_app_configuration_content_selection(const qa_app_configuration_content *content);
void qa_app_configuration_content_set_selection(qa_app_configuration_content *content, qa_app_application_configuration_selection selection);
const void *qa_app_configuration_content_mounts(const qa_app_configuration_content *content);
void qa_app_configuration_content_set_mounts(qa_app_configuration_content *content, const void *mounts);
const void *qa_app_configuration_content_catalog(const qa_app_configuration_content *content);
void qa_app_configuration_content_set_catalog(qa_app_configuration_content *content, const void *catalog);
bool qa_app_configuration_content_q3_product(const qa_app_configuration_content *content, const void **out);
void qa_app_configuration_content_set_q3_product(qa_app_configuration_content *content, const void *product);
void qa_app_configuration_content_close(qa_app_configuration_content *content);
/* TS adoptUnifiedProfile's `{ catalog, mounts, q3Product, close, selection }` over a loaded content: shares its
 * catalog, mounts and Q3 product (owned by the loaded content), selection from its recipe (map.entities,
 * engineBehavior, match, combat, movement, timing); closing runs close(close_ctx). NULL only on OOM. */
qa_app_configuration_content *qa_app_configuration_content_over_loaded(qa_app_loaded_content *loaded,
    void (*close)(void *ctx), void *close_ctx);
qa_launch_preset qa_app_application_preset(qa_app_content_host host, const qa_app_options *options);
qa_launch_preset qa_app_application_configuration_preset(qa_app_content_host host, const qa_app_options *options);
/* --- Concrete catalog composition (TS content.ts over the landed
 * qa_content_catalog). Owned backing for a built preset; the preset
 * borrows from it and from the catalog/options. --- */
typedef struct qa_app_application_preset_result {
  qa_launch_preset preset;
  char **strings; size_t string_count, string_cap;
  qa_provider_reference weapons_storage[1];
  qa_execution_selection execution_storage[1];
  qa_resource_request artifact_storage[1];
  qa_provider_timing timing_storage[3];
  qa_provider_id ordering_storage[3];
  qa_app_options resolved_options; bool has_resolved_options;
} qa_app_application_preset_result;
void qa_app_application_preset_result_free(qa_app_application_preset_result *result);
typedef struct qa_app_source_selection_result {
  qa_app_application_source_selection selection;
  char *source_provider, *match_provider; /* owned */
} qa_app_source_selection_result;
void qa_app_source_selection_result_free(qa_app_source_selection_result *result);
/* TS applicationDiscoversMods (expectedProducts lookup by options->product). */
bool qa_app_application_discovers_mods_native(const qa_app_options *options, bool has_recipe);
/* TS applicationSourceSelection. False only on OOM. */
bool qa_app_application_source_selection_for_catalog(const qa_content_catalog *catalog,
    const char *product, const char *rules, qa_app_source_selection_result *out);
/* TS movementContentProduct. */
const qa_catalog_discovered_product *qa_app_movement_content_product(const qa_content_catalog *catalog, const char *movement_product);
/* TS resolveApplicationMovement: options with movement and movementProduct taken
 * from the movement product (the TS spread). The result is a shallow copy:
 * movement_product borrows the catalog's product id, every other string is
 * options'; do not qa_app_options_destroy it. Raises as catalog.product /
 * movementContentProduct do for an unknown or uninstalled product. */
qa_app_options qa_app_resolve_application_movement(const qa_content_catalog *catalog, const qa_app_options *options);
/* TS applicationPreset / applicationConfigurationPreset. False only on
 * OOM; selection errors abort with the exact TS messages. The preset
 * result borrows catalog product ids and options strings; keep both
 * alive while using it. native_sources NULL = TS undefined. */
typedef struct qa_app_native_provider_sources { qa_provider_reference movement, character; } qa_app_native_provider_sources;
bool qa_app_application_preset_for_catalog(const qa_content_catalog *catalog, const qa_app_options *options,
    const qa_app_native_provider_sources *native_sources, const qa_app_content_source *presentation_source,
    qa_app_application_preset_result *out);
bool qa_app_application_configuration_preset_for_catalog(const qa_content_catalog *catalog, const qa_app_options *options,
    const qa_app_native_provider_sources *native_sources, qa_app_application_preset_result *out);
/* Concrete archive inspector for qa_discover_installed_content
 * (TS openArchive over qa_open_archive). */
qa_catalog_archive_inspector qa_app_default_archive_inspector(void);
/* Concrete loadApplicationContent. installed_catalog NULL discovers
 * one (owned by the result). presentation_source NULL = TS undefined.
 * pure NULL = TS undefined. guest_host supplies the LoadedApplication-
 * Content prepare steps (mods/grapple/weapon behaviors) and may be
 * zeroed when the recipe needs none of them; the catalog/mount/world
 * assembly itself is concrete. Returns NULL only on OOM or when a
 * required guest preparation has no landed concrete host (see the
 * .c note); TS validation errors abort with the exact messages.
 * options is taken over by the content (destroyed with it, or on a NULL
 * return). restored_recipe is borrowed: keep it alive while the content
 * lives.
 * Resource references the content keeps past a mount set (pure-policy
 * recipe resources, map sidecars) are owned copies it releases. */
qa_app_loaded_content *qa_app_load_application_content_native(qa_app_content_host guest_host, qa_app_options options,
    const qa_executable_recipe *restored_recipe, bool has_recipe, const qa_pure_mount_policy *pure,
    qa_content_catalog *installed_catalog, const qa_app_content_source *presentation_source);
qa_app_application_world qa_app_loaded_content_world(const qa_app_loaded_content *content);
/* TS preparedQuakeC / preparedQ3Game / preparedQ2Game (NULL is TS null; owned by the content).
 * The Q2 guest is a qa_app_sim_prepared_classic_guest or qa_app_sim_prepared_rerelease_guest per *out_edition. */
struct qa_app_sim_prepared_quakec_source;
struct qa_app_prepared_q3_game;
const struct qa_app_sim_prepared_quakec_source *qa_app_loaded_content_prepared_quakec(const qa_app_loaded_content *content);
const struct qa_app_prepared_q3_game *qa_app_loaded_content_prepared_q3_game(const qa_app_loaded_content *content);
const void *qa_app_loaded_content_prepared_q2_game(const qa_app_loaded_content *content, const char **out_edition);
/* TS nativeCommandContext producer over preparedQ2Game.execution; NULL is TS preparedQ2Game null. Owned by the content. */
const struct qa_command_producer *qa_app_loaded_content_native_command_producer(qa_app_loaded_content *content);
/* C-only: keeps record alive until the content is destroyed (release runs newest first). */
void qa_app_loaded_content_keep(qa_app_loaded_content *content, void *record, void (*release)(void *record));
const qa_app_application_map_sidecar *qa_app_loaded_content_map_sidecars(const qa_app_loaded_content *content, size_t *out_count);
/* TS LoadedApplicationContent.retainMainMounts: release fn + ctx. */
typedef struct qa_app_main_mount_lease { void *ctx; void (*release)(void *ctx); } qa_app_main_mount_lease;
qa_app_main_mount_lease qa_app_loaded_content_retain_main_mounts(qa_app_loaded_content *content);
/* TS applicationOptionsForRecipe. Result borrows options/catalog
 * strings except its owned copies of product/map/map_product/
 * movement_product/character_model; free those fields only via
 * qa_app_options_for_recipe_free (do not qa_app_options_destroy it). */
qa_app_options qa_app_application_options_for_recipe(const qa_app_options *options, const qa_content_catalog *catalog,
                                                     const qa_executable_recipe *recipe);
void qa_app_options_for_recipe_free(qa_app_options *options);
/* --- Concrete remote/configuration forms (TS openRemoteContent /
 * openRemoteApplicationContent / remoteConfigurationContent /
 * openApplicationConfigurationContent over the landed catalog,
 * mounts/paths and user-data ports). Returns are NULL only on OOM;
 * TS validation errors abort with the exact messages. --- */
/* TS assertCurrent closure: returns the caller's cancellation error
 * text, or NULL when the selection is still current. */
typedef const char *(*qa_app_assert_current_fn)(void *ctx);
/* TS RemoteContentMounts. String/selection/catalog/mounts owned;
 * release with qa_app_remote_content_mounts_destroy (close() first
 * mirrors mounts.close() without releasing the catalog). */
typedef struct qa_app_remote_content_mounts {
  qa_remote_content_selection selection;
  qa_content_catalog *catalog; /* effective (possibly Q3-restricted) */
  const qa_catalog_discovered_product *product;
  qa_content_mounted *mounts;
  char *write_root, *base_write_root; /* owned */
  bool has_q3_product;                /* false mirrors TS null */
  qa_q3_application_product q3_product;
  void *backing; /* owned plan/discovery/Q3 backing */
} qa_app_remote_content_mounts;
/* TS openRemoteContent. `roots` contributes corpusRoot /
 * userContentRoot / q3Product / startupCommands only; it is
 * borrowed for the call. assert_current may be NULL. */
qa_app_remote_content_mounts *qa_app_open_remote_content_native(const qa_app_options *roots,
    const qa_remote_content_selection *requested, qa_app_assert_current_fn assert_current,
    void *assert_ctx, int64_t generation);
void qa_app_remote_content_mounts_close(qa_app_remote_content_mounts *mounts);
void qa_app_remote_content_mounts_destroy(qa_app_remote_content_mounts *mounts);
/* TS openRemoteApplicationContent (MountedApplicationContent rides
 * qa_app_configuration_content). Options borrowed for the call. */
qa_app_configuration_content *qa_app_open_remote_application_content_native(const qa_app_options *options);
/* TS remoteConfigurationContent: shares content's catalog/mounts;
 * closing the result closes content. Options borrowed. */
qa_app_configuration_content *qa_app_remote_configuration_content_native(const qa_app_options *options,
    qa_app_configuration_content *content);
/* TS ApplicationConfigurationRequest (native form; the host-shaped
 * qa_app_configuration_request above is unchanged). */
typedef struct qa_app_configuration_request_native {
  const char *kind; /* "launch" | "recipe" */
  const qa_launch_preset *preset;  /* launch */
  qa_launch_choice choice;         /* launch */
  const qa_executable_recipe *recipe; /* recipe */
} qa_app_configuration_request_native;
/* TS openApplicationConfigurationContent. catalog borrowed;
 * q3_product NULL mirrors TS undefined. */
qa_app_configuration_content *qa_app_open_application_configuration_content_native(
    qa_content_catalog *catalog, qa_app_configuration_request_native request,
    const qa_q3_application_product *q3_product);
/* TS openInitialConfigurationContent(options, recipe?, installedCatalog?) (configuration.ts) over the
 * concrete catalog. recipe / installed_catalog NULL are TS undefined; a discovered catalog is owned by
 * the result. Writes options->q3_product when the product is Q3. NULL only on OOM. */
qa_app_configuration_content *qa_app_open_initial_configuration_content_native(qa_app_options *options,
    const qa_executable_recipe *recipe, qa_content_catalog *installed_catalog);
/* Full TS selection.timing list (the single `timing` field above
 * carries the movement entry, as qa_app_configuration_movement_
 * dialect expects). Borrowed; count 0 when unset. */
const qa_provider_timing *qa_app_configuration_content_timing(const qa_app_configuration_content *content,
    size_t *out_count);
/* Concrete Q3 product value; false mirrors an absent q3Product. */
bool qa_app_configuration_content_q3_value(const qa_app_configuration_content *content,
    qa_q3_application_product *out);
/* --- Startup selection exports (additive; startup-selection.ts reads
 * these content.ts / mod-selection.ts / q3-product.ts pieces). --- */
/* The concrete openMountPlan + readQvmCompatibility launch services. */
qa_launch_services qa_app_native_launch_services(void);
/* TS applicationPlayerProducts. has_native_source false is TS undefined.
 * Results borrow catalog product ids. */
void qa_app_application_player_products(const qa_content_catalog *catalog, const qa_app_options *options,
    bool has_native_source, bool native_source, const char **out_movement, const char **out_character);
/* TS prepareQ3ApplicationProduct over the concrete host. *backing (NULL
 * unless the product is Q3) owns any restricted catalog in *out; free it
 * with qa_app_q3_product_backing_free once that catalog is unused. */
struct qa_app_prepared_q3_product;
void qa_app_prepare_q3_application_product_native(qa_content_catalog *catalog, const char *content,
    const qa_app_options *options, struct qa_app_prepared_q3_product *out, void **backing);
void qa_app_q3_product_backing_free(void *backing);
/* TS applicationModChoices over the concrete gameplay-mod and weapon-
 * behavior hosts, with the weapon-behavior conflict augmentation. The
 * choices are owned by *backing (free with qa_app_mod_choices_backing_free). */
struct qa_app_mod_choices;
const struct qa_app_mod_choices *qa_app_application_mod_choices_native(qa_content_catalog *catalog, void **backing);
void qa_app_mod_choices_backing_free(void *backing);
/* catalog.mountsFor(content) opened with its default order under plan id
 * createMountPlanId(plan_name_space, hex(content)); demo selects
 * q3Restriction "demo". Aborts on OOM. */
typedef struct qa_app_product_mounts qa_app_product_mounts;
qa_app_product_mounts *qa_app_open_product_mounts(qa_content_catalog *catalog, qa_content_id content,
    const char *plan_name_space, bool demo);
qa_content_mounted *qa_app_product_mounts_mounted(qa_app_product_mounts *mounts);
void qa_app_product_mounts_destroy(qa_app_product_mounts *mounts);
#endif
