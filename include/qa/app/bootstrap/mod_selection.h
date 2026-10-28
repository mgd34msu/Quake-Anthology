/* Port of src/app/bootstrap/mod-selection.ts. Mount preparation,
 * weapon-behavior choices (weapon-behavior-selection.ts) and the
 * mounted mod preparers (simulation mod preparers) are other slices and
 * arrive through the host below. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MOD_SELECTION_H
#define QA_APP_BOOTSTRAP_MOD_SELECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/catalog/index.h"
#include "qa/content/mods/catalog.h"
#include "qa/contracts/content.h"
#include "qa/contracts/mods.h"
#include "qa/world/session/mods.h"
/* TS ApplicationModChoice. implementation_kind:
 * "unavailable"|"gameplay"|"weapon-behavior". Rows borrowed. */
typedef struct qa_app_mod_choice {
  qa_mod_description description;
  const char *implementation_kind;
  const qa_resolved_gameplay_mod *gameplay;                    /* gameplay */
  const qa_resolved_weapon_behavior_selection *weapon_behavior; /* weapon-behavior */
} qa_app_mod_choice;
typedef struct qa_app_mod_selection_host {
  void *ctx;
  /* TS discoverGameplayMods over opened mounts for a product.
   * Returns a borrowed error string or NULL; entries borrowed. */
  const char *(*discover_mods)(void *ctx, const qa_catalog_discovered_product *product,
      const qa_discovered_gameplay_mod **out, size_t *out_count);
  /* TS applicationWeaponBehaviorChoices rows for a product id. */
  const char *(*weapon_behavior_choices)(void *ctx, const char *product_id,
      const qa_app_mod_choice **out, size_t *out_count);
  /* TS prepareMounted{QuakeC,Qvm,Native}Mod by declaration runtime. */
  const char *(*prepare_mod)(void *ctx, const qa_mod_description *description,
      const qa_resolved_gameplay_mod *mod, qa_prepared_mod *out);
  /* TS forContent(content): validation hook (mounts stay host-side). */
  const char *(*for_content)(void *ctx, qa_content_id content);
} qa_app_mod_selection_host;
/* Owned choice array; free the array only (rows borrow host/catalog
 * storage; synthesized description strings are static/owned by the
 * returned scratch block freed with the array). */
typedef struct qa_app_mod_choices {
  qa_app_mod_choice *items; size_t count;
  void *scratch;
} qa_app_mod_choices;
qa_app_mod_choices qa_app_application_mod_choices(const qa_content_catalog *catalog,
    const qa_app_mod_selection_host *host);
void qa_app_mod_choices_destroy(qa_app_mod_choices *choices);
/* TS applyApplicationMods. On a non-empty selection, fills a shallow
 * recipe copy with owned mods/weaponBehaviors pointer arrays (free
 * the arrays, not the pointees). Aborts with the TS Error text. */
typedef struct qa_app_applied_mods {
  qa_executable_recipe recipe;
  const qa_resolved_gameplay_mod **mods; size_t mod_count;
  qa_resolved_weapon_behavior_selection *weapon_behaviors; size_t weapon_behavior_count;
  bool unchanged;
} qa_app_applied_mods;
qa_app_applied_mods qa_app_apply_application_mods(const qa_executable_recipe *recipe,
    const qa_app_mod_choice *choices, size_t choice_count,
    const qa_mod_selection *enabled, size_t enabled_count);
void qa_app_applied_mods_destroy(qa_app_applied_mods *applied);
/* TS prepareApplicationMods. purpose "gameplay"|"presentation".
 * declaration_text is the host's JSON for declaration comparison:
 * equal text means JSON.stringify equality. */
typedef struct qa_app_prepare_mods_result {
  qa_prepared_mod *items; size_t count;
} qa_app_prepare_mods_result;
bool qa_app_prepare_application_mods(const qa_content_catalog *catalog,
    const qa_executable_recipe *recipe, const char *purpose,
    const qa_app_mod_selection_host *host,
    const char *(*declaration_text)(void *ctx, const qa_resolved_gameplay_mod *mod), void *declaration_ctx,
    qa_app_prepare_mods_result *out, const char **error_out);
#endif
