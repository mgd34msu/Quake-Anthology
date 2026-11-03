/* Port of src/app/bootstrap/mod-selection.ts: applicationModChoices,
 * applyApplicationMods and prepareApplicationMods. TS async members are
 * synchronous (PORTING.md); a TS throw is qa_raise("Error"). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_MOD_SELECTION_H
#define QA_APP_BOOTSTRAP_MOD_SELECTION_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/catalog/index.h"
#include "qa/content/mods/catalog.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/mods.h"
#include "qa/world/session/mods.h"
/* TS ApplicationModChoice. implementation_kind:
 * "unavailable"|"gameplay"|"weapon-behavior". Rows borrow the choices' storage. */
typedef struct qa_app_mod_choice {
  qa_mod_description description;
  const char *implementation_kind;
  const qa_resolved_gameplay_mod *gameplay;                    /* gameplay */
  const qa_resolved_weapon_behavior_selection *weapon_behavior; /* weapon-behavior */
} qa_app_mod_choice;
/* TS applicationModChoices(catalog). The result owns its rows and everything they
 * borrow: discovered declarations, weapon-behavior choice lists, synthesized
 * descriptions and the conflict lists. Product mounts are closed per product, as
 * the TS MountPreparationScope closes them when discovery ends. */
typedef struct qa_app_mod_choices {
  qa_app_mod_choice *items; size_t count;
  void *scratch;
} qa_app_mod_choices;
qa_app_mod_choices qa_app_application_mod_choices(qa_content_catalog *catalog);
void qa_app_mod_choices_destroy(qa_app_mod_choices *choices);
/* TS applyApplicationMods. On a non-empty selection, fills a shallow
 * recipe copy with owned mods/weaponBehaviors pointer arrays (free
 * the arrays, not the pointees). Raises the TS Error text. */
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
/* TS forContent(content): the content's mounted content. The prepared mods borrow
 * it, so it must outlive them (LoadedApplicationContent keeps it until close). */
typedef qa_content_mounted *(*qa_app_mod_for_content_fn)(void *ctx, qa_content_id content);
/* TS prepareApplicationMods(catalog, recipe, forContent, purpose); purpose
 * "gameplay" (NULL, the TS default) or "presentation". The result owns its prepared
 * mods and the rediscovered declarations and descriptions they borrow. Raises the
 * TS Error texts. */
typedef struct qa_app_prepared_mods {
  qa_prepared_mod *items; size_t count;
  void *backing;
} qa_app_prepared_mods;
qa_app_prepared_mods qa_app_prepare_application_mods(qa_content_catalog *catalog,
    const qa_executable_recipe *recipe, qa_app_mod_for_content_fn for_content, void *for_content_ctx,
    const char *purpose);
void qa_app_prepared_mods_destroy(qa_app_prepared_mods *prepared);
#endif
