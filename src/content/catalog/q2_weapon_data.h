/* Mirrored weapon/icon data for the catalog port. TS owners:
 * src/content/q2/foundation/weapons/definitions.ts (base),
 * src/content/q2/missionpacks/weapons/definitions.ts
 * (xatrix/rogue), src/content/q2/foundation/items.ts (icons),
 * src/content/q2/missionpacks/items.ts (icons),
 * src/content/q3/foundation/arsenal.ts + q3/base/shared/
 * items.ts (Q3 icons), src/content/q1/missionpacks/types.ts
 * (mission weapons), src/content/q1/foundation/
 * precache-world.ts (filtered Q1 precaches in weapons.c).
 * Those modules are unported. GPL-2.0-or-later. */
#ifndef QA_CONTENT_CATALOG_Q2_WEAPON_DATA_H
#define QA_CONTENT_CATALOG_Q2_WEAPON_DATA_H

#include "qa/content/catalog/weapons.h"

static const qa_catalog_q2_weapon_definition Q2_BASE_DATA[] = {
  {"blaster", "q2:weapon_blaster", NULL, "models/weapons/v_blast/tris.md2", ""},
  {"shotgun", "q2:weapon_shotgun", "q2:ammo_shells", "models/weapons/v_shotg/tris.md2", "models/weapons/g_shotg/tris.md2"},
  {"supershotgun", "q2:weapon_supershotgun", "q2:ammo_shells", "models/weapons/v_shotg2/tris.md2", "models/weapons/g_shotg2/tris.md2"},
  {"machinegun", "q2:weapon_machinegun", "q2:ammo_bullets", "models/weapons/v_machn/tris.md2", "models/weapons/g_machn/tris.md2"},
  {"chaingun", "q2:weapon_chaingun", "q2:ammo_bullets", "models/weapons/v_chain/tris.md2", "models/weapons/g_chain/tris.md2"},
  {"grenades", "q2:ammo_grenades", "q2:ammo_grenades", "models/weapons/v_handgr/tris.md2", "models/items/ammo/grenades/medium/tris.md2"},
  {"grenadelauncher", "q2:weapon_grenadelauncher", "q2:ammo_grenades", "models/weapons/v_launch/tris.md2", "models/weapons/g_launch/tris.md2"},
  {"rocketlauncher", "q2:weapon_rocketlauncher", "q2:ammo_rockets", "models/weapons/v_rocket/tris.md2", "models/weapons/g_rocket/tris.md2"},
  {"hyperblaster", "q2:weapon_hyperblaster", "q2:ammo_cells", "models/weapons/v_hyperb/tris.md2", "models/weapons/g_hyperb/tris.md2"},
  {"railgun", "q2:weapon_railgun", "q2:ammo_slugs", "models/weapons/v_rail/tris.md2", "models/weapons/g_rail/tris.md2"},
  {"bfg", "q2:weapon_bfg", "q2:ammo_cells", "models/weapons/v_bfg/tris.md2", "models/weapons/g_bfg/tris.md2"},
};
static const qa_catalog_q2_weapon_definition Q2_XATRIX_DATA[] = {
  {"trap", "q2:ammo_trap", "q2:ammo_trap", "models/weapons/v_trap/tris.md2", "models/weapons/g_trap/tris.md2"},
  {"ionripper", "q2:weapon_boomer", "q2:ammo_cells", "models/weapons/v_boomer/tris.md2", "models/weapons/g_boom/tris.md2"},
  {"phalanx", "q2:weapon_phalanx", "q2:ammo_magslug", "models/weapons/v_shotx/tris.md2", "models/weapons/g_shotx/tris.md2"},
};
static const qa_catalog_q2_weapon_definition Q2_ROGUE_DATA[] = {
  {"tesla", "q2:ammo_tesla", "q2:ammo_tesla", "models/weapons/v_tesla/tris.md2", "models/ammo/am_tesl/tris.md2"},
  {"proxlauncher", "q2:weapon_proxlauncher", "q2:ammo_prox", "models/weapons/v_launch/tris.md2", "models/weapons/g_launch/tris.md2"},
  {"chainfist", "q2:weapon_chainfist", NULL, "models/weapons/v_chainf/tris.md2", "models/weapons/g_chainf/tris.md2"},
  {"disintegrator", "q2:weapon_disintegrator", "q2:ammo_disruptor", "models/weapons/v_dist/tris.md2", "models/weapons/g_dist/tris.md2"},
  {"etf_rifle", "q2:weapon_etf_rifle", "q2:ammo_flechettes", "models/weapons/v_etf_rifle/tris.md2", "models/weapons/g_etf_rifle/tris.md2"},
  {"heatbeam", "q2:weapon_plasmabeam", "q2:ammo_cells", "models/weapons/v_beamer/tris.md2", "models/weapons/g_beamer/tris.md2"},
};

typedef struct q2_icon_entry {
  const char *item;
  const char *icon;
} q2_icon_entry;
/* q2BaseItemIcons: ammunition first, then base weapons. */
static const q2_icon_entry Q2_BASE_ICONS[] = {
  {"q2:ammo_shells", "a_shells"}, {"q2:ammo_bullets", "a_bullets"},
  {"q2:ammo_cells", "a_cells"}, {"q2:ammo_rockets", "a_rockets"},
  {"q2:ammo_slugs", "a_slugs"}, {"q2:ammo_grenades", "a_grenades"},
  {"q2:weapon_blaster", "w_blaster"}, {"q2:weapon_shotgun", "w_shotgun"},
  {"q2:weapon_supershotgun", "w_sshotgun"}, {"q2:weapon_machinegun", "w_machinegun"},
  {"q2:weapon_chaingun", "w_chaingun"}, {"q2:ammo_grenades", "a_grenades"},
  {"q2:weapon_grenadelauncher", "w_glauncher"}, {"q2:weapon_rocketlauncher", "w_rlauncher"},
  {"q2:weapon_hyperblaster", "w_hyperblaster"}, {"q2:weapon_railgun", "w_railgun"},
  {"q2:weapon_bfg", "w_bfg"},
};
/* q2MissionWeaponIcons: mission ammo, then displayed weapons
 * (trap/tesla have no display entry). */
static const q2_icon_entry Q2_MISSION_ICONS[] = {
  {"q2:ammo_magslug", "a_mslugs"}, {"q2:ammo_trap", "a_trap"},
  {"q2:ammo_flechettes", "a_flechettes"}, {"q2:ammo_prox", "a_prox"},
  {"q2:ammo_tesla", "a_tesla"}, {"q2:ammo_disruptor", "a_disruptor"},
  {"q2:weapon_boomer", "w_ripper"}, {"q2:weapon_phalanx", "w_phallanx"},
  {"q2:weapon_proxlauncher", "w_proxlaunch"}, {"q2:weapon_chainfist", "w_chainfist"},
  {"q2:weapon_disintegrator", "w_disintegrator"}, {"q2:weapon_etf_rifle", "w_etf_rifle"},
  {"q2:weapon_plasmabeam", "w_heatbeam"},
};

typedef struct q3_weapon_icon_entry {
  const char *item;
  const char *weapon_icon; /* nullable */
  const char *ammo_icon;   /* nullable */
  int missionpack_only;
} q3_weapon_icon_entry;
/* Q3_WEAPON_ITEMS order; icons from itemList IT_WEAPON/IT_AMMO by
 * tag (baseq3 list is the first 36 definitions, so the last
 * three weapons resolve only for missionpack). */
static const q3_weapon_icon_entry Q3_WEAPON_ICONS[] = {
  {"q3:weapon/gauntlet", "icons/iconw_gauntlet", NULL, 0},
  {"q3:weapon/machinegun", "icons/iconw_machinegun", "icons/icona_machinegun", 0},
  {"q3:weapon/shotgun", "icons/iconw_shotgun", "icons/icona_shotgun", 0},
  {"q3:weapon/grenadelauncher", "icons/iconw_grenade", "icons/icona_grenade", 0},
  {"q3:weapon/rocketlauncher", "icons/iconw_rocket", "icons/icona_rocket", 0},
  {"q3:weapon/lightning", "icons/iconw_lightning", "icons/icona_lightning", 0},
  {"q3:weapon/railgun", "icons/iconw_railgun", "icons/icona_railgun", 0},
  {"q3:weapon/plasmagun", "icons/iconw_plasma", "icons/icona_plasma", 0},
  {"q3:weapon/bfg", "icons/iconw_bfg", "icons/icona_bfg", 0},
  {"q3:weapon/grapple", "icons/iconw_grapple", NULL, 0},
  {"q3:weapon/nailgun", "icons/iconw_nailgun", "icons/icona_nailgun", 1},
  {"q3:weapon/proxlauncher", "icons/iconw_proxlauncher", "icons/icona_proxlauncher", 1},
  {"q3:weapon/chaingun", "icons/iconw_chaingun", "icons/icona_chaingun", 1},
};

#endif
