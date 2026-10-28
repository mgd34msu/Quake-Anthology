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
qa_app_prepared_weapon_behavior qa_app_prepare_weapon_behavior(qa_app_weapon_behavior_host host, qa_content_catalog *catalog,
                                                               qa_resolved_weapon_behavior_selection selection);
#endif
