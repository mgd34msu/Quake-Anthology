/* Port of src/content/q2/foundation/weapons/definitions.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/weapons/definitions.h"
#include <string.h>

static const double p_blaster[] = {19, 32}, f_blaster[] = {5};
static const double p_shotgun[] = {22, 28, 34}, f_shotgun[] = {8, 9};
static const double p_super[] = {29, 42, 57}, f_super[] = {7};
static const double p_mach[] = {23, 45}, f_mach[] = {4, 5};
static const double p_chain[] = {38, 43, 51, 61};
static const double f_chain[] = {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21};
static const double p_gren[] = {29, 34, 39, 48}, f_gren[] = {12};
static const double p_launch[] = {34, 51, 59}, f_launch[] = {6};
static const double p_rocket[] = {25, 33, 42, 50}, f_rocket[] = {5};
static const double f_hyper[] = {6, 7, 8, 9, 10, 11};
static const double p_rail[] = {56}, f_rail[] = {4};
static const double p_bfg[] = {39, 45, 50, 55}, f_bfg[] = {9, 17};
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define D(name_, item_, cn, ammo_, q, w, vm, wm, pm, al, fl, il, dl, p, f, rep) \
  {name_, item_, cn, ammo_, q, w, vm, wm, pm, al, fl, il, dl, p, N(p), f, N(f), rep, false}
const qa_q2w_base_definition qa_q2w_base_weapons[] = {
  D("blaster", "q2:weapon_blaster", "weapon_blaster", NULL, 0, 0, "models/weapons/v_blast/tris.md2", "", 1, 4, 8, 52, 55, p_blaster, f_blaster, false),
  D("shotgun", "q2:weapon_shotgun", "weapon_shotgun", "q2:ammo_shells", 1, 5, "models/weapons/v_shotg/tris.md2", "models/weapons/g_shotg/tris.md2", 2, 7, 18, 36, 39, p_shotgun, f_shotgun, false),
  D("supershotgun", "q2:weapon_supershotgun", "weapon_supershotgun", "q2:ammo_shells", 2, 10, "models/weapons/v_shotg2/tris.md2", "models/weapons/g_shotg2/tris.md2", 3, 6, 17, 57, 61, p_super, f_super, false),
  D("machinegun", "q2:weapon_machinegun", "weapon_machinegun", "q2:ammo_bullets", 1, 30, "models/weapons/v_machn/tris.md2", "models/weapons/g_machn/tris.md2", 4, 3, 5, 45, 49, p_mach, f_mach, true),
  D("chaingun", "q2:weapon_chaingun", "weapon_chaingun", "q2:ammo_bullets", 1, 60, "models/weapons/v_chain/tris.md2", "models/weapons/g_chain/tris.md2", 5, 4, 31, 61, 64, p_chain, f_chain, true),
  D("grenades", "q2:ammo_grenades", "ammo_grenades", "q2:ammo_grenades", 1, 2, "models/weapons/v_handgr/tris.md2", "models/items/ammo/grenades/medium/tris.md2", 6, 0, 15, 48, 48, p_gren, f_gren, false),
  D("grenadelauncher", "q2:weapon_grenadelauncher", "weapon_grenadelauncher", "q2:ammo_grenades", 1, 5, "models/weapons/v_launch/tris.md2", "models/weapons/g_launch/tris.md2", 7, 5, 16, 59, 64, p_launch, f_launch, false),
  D("rocketlauncher", "q2:weapon_rocketlauncher", "weapon_rocketlauncher", "q2:ammo_rockets", 1, 5, "models/weapons/v_rocket/tris.md2", "models/weapons/g_rocket/tris.md2", 8, 4, 12, 50, 54, p_rocket, f_rocket, false),
  {"hyperblaster", "q2:weapon_hyperblaster", "weapon_hyperblaster", "q2:ammo_cells", 1, 30, "models/weapons/v_hyperb/tris.md2", "models/weapons/g_hyperb/tris.md2", 9, 5, 20, 49, 53, NULL, 0, f_hyper, N(f_hyper), true, false},
  D("railgun", "q2:weapon_railgun", "weapon_railgun", "q2:ammo_slugs", 1, 5, "models/weapons/v_rail/tris.md2", "models/weapons/g_rail/tris.md2", 10, 3, 18, 56, 61, p_rail, f_rail, false),
  D("bfg", "q2:weapon_bfg", "weapon_bfg", "q2:ammo_cells", 50, 50, "models/weapons/v_bfg/tris.md2", "models/weapons/g_bfg/tris.md2", 11, 8, 32, 55, 58, p_bfg, f_bfg, false),
};
const size_t qa_q2w_base_weapon_count = N(qa_q2w_base_weapons);

const qa_q2w_base_definition *qa_q2w_weapon_definition(qa_q2w_name name) {
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++)
    if (strcmp(qa_q2w_base_weapons[i].name, name) == 0) return &qa_q2w_base_weapons[i];
  qa_raise("Error", 0, 0, "Q2 weapon is not registered: %s", name);
}
const qa_q2w_base_definition *qa_q2w_weapon_from_classname(const char *classname) {
  for (size_t i = 0; i < qa_q2w_base_weapon_count; i++)
    if (strcmp(qa_q2w_base_weapons[i].classname, classname) == 0) return &qa_q2w_base_weapons[i];
  return NULL;
}
