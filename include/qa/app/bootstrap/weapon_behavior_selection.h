/* Port of src/app/bootstrap/weapon-behavior-selection.ts. Catalog /
 * mount discovery, mod-selection and guest preparation (landed content
 * catalog + sibling bootstrap slices) are host fn+ctx; request parsing
 * and selection guards are kept here. Async -> synchronous.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_WEAPON_BEHAVIOR_SELECTION_H
#define QA_APP_BOOTSTRAP_WEAPON_BEHAVIOR_SELECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/options.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/core/cvars/index.h"
typedef struct qa_app_weapon_behavior_request { char *product; char *id; } qa_app_weapon_behavior_request;
typedef struct qa_app_weapon_behavior_choice {
  char *id; char *title; char *unavailable; /* NULL = TS null */
  const qa_resolved_weapon_behavior_selection *selection; /* borrowed, NULL = TS null */
} qa_app_weapon_behavior_choice;
/* TS PreparedWeaponBehavior discriminated on kind; payloads opaque. */
/* The payloads of the "qvm" and "rerelease-native" PreparedWeaponBehavior arms (TS artifact/profile/mounts and
 * prepared/declaration/mounts); the payload of a prepared entry of that kind points at one. */
typedef struct qa_resolved_qvm_artifact qa_resolved_qvm_artifact;
typedef struct qa_qvm_weapon_profile qa_qvm_weapon_profile;
typedef struct qa_app_sim_prepared_rerelease_guest qa_app_sim_prepared_rerelease_guest;
typedef struct qa_content_mounted qa_content_mounted;
typedef struct qa_app_prepared_qvm_weapon_behavior {
  const qa_resolved_qvm_artifact *artifact; const qa_qvm_weapon_profile *profile; qa_content_mounted *mounts;
  void *owner; /* C-only: the discovery storage behind artifact and profile (prepareApplicationWeaponBehavior) */
} qa_app_prepared_qvm_weapon_behavior;
typedef struct qa_app_prepared_native_weapon_behavior {
  const qa_app_sim_prepared_rerelease_guest *prepared; const qa_native_weapon_behavior_declaration *declaration; qa_content_mounted *mounts;
} qa_app_prepared_native_weapon_behavior;
/* The payload of the "quakec" arm (TS program/resources/mounts). */
typedef struct qa_compat_qc_program qa_compat_qc_program;
typedef struct qa_app_sim_quakec_resources qa_app_sim_quakec_resources;
typedef struct qa_app_prepared_quakec_weapon_behavior {
  const qa_compat_qc_program *program; const qa_app_sim_quakec_resources *resources; qa_content_mounted *mounts;
} qa_app_prepared_quakec_weapon_behavior;
typedef struct qa_app_prepared_weapon_behavior {
  const char *kind; /* "qvm"|"quakec"|"rerelease-native" */
  qa_resolved_weapon_behavior_selection selection;
  const void *payload; /* borrowed: artifact+profile / program+resources / prepared guest */
  const void *mounts;  /* borrowed */
} qa_app_prepared_weapon_behavior;
typedef struct qa_app_weapon_behavior_host {
  void *ctx;
  /* choicesFromMounts over an opened mount plan for product. */
  qa_app_weapon_behavior_choice *(*choices_for_product)(void *ctx, qa_content_catalog *catalog, const char *product_id, size_t *out_count);
  void (*choices_free)(void *ctx, qa_app_weapon_behavior_choice *choices, size_t count);
  /* mod-selection.ts (sibling slice): applyApplicationMods over choices. */
  qa_executable_recipe (*apply_mods)(void *ctx, qa_content_catalog *catalog, qa_executable_recipe recipe,
                                     const qa_app_weapon_behavior_request *requests, size_t request_count);
  /* discoverInstalledContent with discoverMods (returns new catalog or NULL to keep). */
  qa_content_catalog *(*rediscover_with_mods)(void *ctx, qa_content_catalog *catalog);
  bool (*catalog_has_product)(void *ctx, qa_content_catalog *catalog, const char *product);
  /* prepareApplicationWeaponBehavior's mounted revalidation + kind payload. */
  const char *(*prepare)(void *ctx, qa_content_catalog *catalog, qa_resolved_weapon_behavior_selection selection,
                         const void *mounts, qa_app_prepared_weapon_behavior *out);
  const void *(*for_content)(void *ctx, const char *content_id);
  const char *(*product_expectation_id)(void *ctx, qa_content_catalog *catalog, const char *content_id);
} qa_app_weapon_behavior_host;
qa_app_weapon_behavior_request qa_app_read_weapon_behavior_request(const char *value);
void qa_app_weapon_behavior_request_free(qa_app_weapon_behavior_request *r);
/* Returns options (possibly with weapon behavior filled from cvars). */
qa_app_options qa_app_configured_weapon_behavior_options(qa_app_options options, qa_cvar_registry *cvars);
qa_app_weapon_behavior_choice *qa_app_weapon_behavior_choices(qa_app_weapon_behavior_host host, qa_content_catalog *catalog, const char *product_id, size_t *out_count);
qa_executable_recipe qa_app_select_weapon_behavior(qa_app_weapon_behavior_host host, qa_content_catalog *catalog,
                                                   qa_executable_recipe recipe, qa_app_weapon_behavior_request request,
                                                   qa_resolved_weapon_behavior_selection *selection_storage);
typedef struct qa_app_prepared_configured_recipe { qa_content_catalog *catalog; qa_executable_recipe recipe; } qa_app_prepared_configured_recipe;
qa_app_prepared_configured_recipe qa_app_prepare_configured_recipe(qa_app_weapon_behavior_host host, qa_content_catalog *catalog,
                                                                   const qa_app_options *options, qa_executable_recipe recipe);
/* TS prepareApplicationWeaponBehavior(catalog, selection, forContent): revalidates the
 * selection against the mounted declaration and artifact and prepares its executable
 * (QVM artifact and profile, rerelease native guest, or QuakeC program and resources).
 * The entry's payload is an owned kind record (qa_app_prepared_{qvm,native,quakec}_
 * weapon_behavior); mounts are the forContent mounts (borrowed). Raises Error with the
 * TS messages. Release with qa_app_prepared_weapon_behavior_destroy. */
typedef struct qa_content_mounted *(*qa_app_weapon_behavior_for_content_fn)(void *ctx, qa_content_id content);
qa_app_prepared_weapon_behavior qa_app_prepare_application_weapon_behavior(qa_content_catalog *catalog,
    qa_resolved_weapon_behavior_selection selection, qa_app_weapon_behavior_for_content_fn for_content, void *for_content_ctx);
void qa_app_prepared_weapon_behavior_destroy(qa_app_prepared_weapon_behavior *prepared);
/* Concrete TS applicationWeaponBehaviorChoices + choicesFromMounts
 * over the landed catalog/mount/compat services (the host's
 * choices_for_product seam, filled by content.c's native loader).
 * Rows and selections borrow backing storage freed as one unit. */
typedef struct qa_app_weapon_behavior_choice_list {
  qa_app_weapon_behavior_choice *items; size_t count;
  void *backing; /* owned mounts/discovery/selection storage */
} qa_app_weapon_behavior_choice_list;
qa_app_weapon_behavior_choice_list qa_app_weapon_behavior_choices_native(qa_content_catalog *catalog,
    const char *product_id);
void qa_app_weapon_behavior_choice_list_free(qa_app_weapon_behavior_choice_list *list);
#endif
