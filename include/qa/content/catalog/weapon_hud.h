/*
 * Port of src/content/catalog/weapon-hud.ts. Icon name strings
 * for image icons are malloc'd into the result's owned list.
 * Q2/Q3 icon tables are mirrored from their TS owners
 * (content/q2 foundation/missionpacks items, content/q3
 * foundation arsenal + base shared items), which are unported;
 * only the item/icon fields this file reads are mirrored.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_CATALOG_WEAPON_HUD_H
#define QA_CONTENT_CATALOG_WEAPON_HUD_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/catalog/equipment.h"
#include "qa/content/catalog/index.h"
#include "qa/contracts/content.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/ui.h"

/* TS WeaponHudIcons; NULL icon pointers spell the TS nulls. */
typedef struct qa_weapon_hud_icons {
  qa_weapon_hud_icon weapon;
  bool has_weapon;
  qa_weapon_hud_icon selected_weapon;
  bool has_selected_weapon;
  qa_weapon_hud_icon ammo;
  bool has_ammo;
  char **owned_paths;
  size_t owned_count;
} qa_weapon_hud_icons;

/* TS weaponHudIcons. *out_found false is the TS null return. */
bool qa_weapon_hud_icons_for(qa_provider_reference source,
                             const qa_catalog_product_expectation *product,
                             qa_item_id item, qa_weapon_hud_icons *out,
                             bool *out_found);
void qa_weapon_hud_icons_free(qa_weapon_hud_icons *icons);

/* TS weaponHudResources. */
bool qa_weapon_hud_resources(qa_provider_reference source,
                             const qa_catalog_product_expectation *product,
                             qa_catalog_resource_list *out);

#endif
