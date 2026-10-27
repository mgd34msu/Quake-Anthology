/*
 * Port of src/content/catalog/weapons.ts. Weapon definition data
 * (names/items/models) is mirrored privately in
 * src/content/catalog/q2_weapon_data.h from its TS owners
 * (content/q2 foundation + missionpacks, content/q1
 * missionpacks/precache), which are unported. The
 * qa_q2_registered_weapon_resources input is the definition list
 * the TS reads through Q2Weapons.registeredDefinitions().
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_WEAPONS_H
#define QA_CONTENT_CATALOG_WEAPONS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/equipment.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"

typedef struct qa_classic_rerelease_providers {
  qa_provider_id classic;
  qa_provider_id rerelease;
} qa_classic_rerelease_providers;
extern const qa_classic_rerelease_providers qa_q1_weapon_providers;
extern const qa_classic_rerelease_providers qa_q1_hipnotic_weapon_providers;
extern const qa_classic_rerelease_providers qa_q2_weapon_providers;

/* Mirrored Q2 weapon definition fields this module reads (TS
 * owners: src/content/q2/foundation/weapons/definitions.ts and
 * src/content/q2/missionpacks/weapons/definitions.ts). */
typedef struct qa_catalog_q2_weapon_definition {
  const char *name;
  qa_item_id item;
  qa_item_id ammo; /* nullable */
  const char *view_model;
  const char *world_model;
} qa_catalog_q2_weapon_definition;

extern const qa_catalog_q2_weapon_definition *const qa_q2_base_weapons_catalog;
extern const size_t qa_q2_base_weapon_catalog_count;
extern const qa_catalog_q2_weapon_definition *const qa_xatrix_weapon_definitions_catalog;
extern const size_t qa_xatrix_weapon_definition_catalog_count;
extern const qa_catalog_q2_weapon_definition *const qa_rogue_weapon_definitions_catalog;
extern const size_t qa_rogue_weapon_definition_catalog_count;

/* TS supportsSelectedWeaponProduct. */
bool qa_supports_selected_weapon_product(
    const qa_catalog_product_expectation *product);

/* TS canonicalWeaponSource. *owned_provider is a malloc'd
 * provider string when the result synthesizes one, else NULL
 * (the result then borrows map/weapon strings). */
qa_provider_reference qa_canonical_weapon_source(
    qa_provider_reference map, qa_provider_reference weapon,
    const qa_content_catalog *catalog, char **owned_provider);

/* Deduplicated owned path list. */
typedef struct qa_catalog_path_list {
  char **values;
  size_t count;
} qa_catalog_path_list;
void qa_catalog_path_list_free(qa_catalog_path_list *list);

/* TS q2RegisteredWeaponResources. */
bool qa_q2_registered_weapon_resources(
    const qa_catalog_q2_weapon_definition *definitions, size_t count,
    bool rerelease, qa_catalog_path_list *out);

/* TS selectedWeaponResources / weaponResources. */
bool qa_selected_weapon_resources(qa_provider_reference map,
                                  const qa_provider_reference *weapons,
                                  size_t weapon_count,
                                  const qa_content_catalog *catalog,
                                  qa_catalog_resource_list *out);
bool qa_weapon_resources(qa_provider_reference map,
                         const qa_provider_reference *weapons,
                         size_t weapon_count,
                         const qa_content_catalog *catalog,
                         qa_catalog_resource_list *out);

/* Mutable timing list (TS ProviderTiming[] for admitWeaponTiming). */
typedef struct qa_catalog_timing_list {
  qa_provider_timing *values;
  size_t count;
  size_t capacity;
} qa_catalog_timing_list;
bool qa_catalog_timing_push(qa_catalog_timing_list *list,
                            qa_provider_timing timing);
void qa_catalog_timing_list_free(qa_catalog_timing_list *list);

/* TS selectedWeaponTiming (entries own numeric.id; free with
 * qa_catalog_timing_list_free after wrapping, or
 * qa_selected_weapon_timing_free). */
qa_provider_timing *qa_selected_weapon_timing(
    qa_provider_reference map, const qa_provider_reference *weapons,
    size_t weapon_count, const qa_content_catalog *catalog, size_t *out_count);
void qa_selected_weapon_timing_free(qa_provider_timing *values, size_t count);

/* TS admitWeaponTiming: appends to the list or verifies an
 * existing entry; conflicts abort with the exact TS Error. */
void qa_admit_weapon_timing(qa_catalog_timing_list *timing,
                            qa_provider_timing weapon);

#endif
