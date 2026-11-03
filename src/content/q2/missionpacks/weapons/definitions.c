/* Port of src/content/q2/missionpacks/weapons/definitions.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/weapons/definitions.h"

static const double p_trap[] = {29, 34, 39, 48}, f_trap[] = {12};
static const double p_ion[] = {36}, f_ion[] = {5};
static const double p_phalanx[] = {29, 42, 55}, f_phalanx[] = {7, 8};
static const double p_tesla[] = {21}, f_tesla[] = {2};
static const double p_prox[] = {34, 51, 59}, f_prox[] = {6};
static const double f_chainfist[] = {8, 9, 16, 17, 18, 30, 31};
static const double p_dis[] = {14, 19, 23}, f_dis[] = {5};
static const double p_etf[] = {18, 28}, f_etf[] = {6, 7};
static const double p_heat[] = {35}, f_heat[] = {9, 10, 11, 12};
#define N(a) (sizeof(a) / sizeof((a)[0]))
#define D(name_, item_, cn, ammo_, q, w, vm, wm, pm, al, fl, il, dl, p, f, rep) \
  {name_, item_, cn, ammo_, q, w, vm, wm, pm, al, fl, il, dl, p, N(p), f, N(f), rep, false}

const qa_q2w_definition qa_q2mp_xatrix_weapon_definitions[] = {
  D("trap", "q2:ammo_trap", "ammo_trap", "q2:ammo_trap", 1, 1, "models/weapons/v_trap/tris.md2", "models/weapons/g_trap/tris.md2", 0, 0, 15, 48, 48, p_trap, f_trap, false),
  D("ionripper", "q2:weapon_boomer", "weapon_boomer", "q2:ammo_cells", 2, 10, "models/weapons/v_boomer/tris.md2", "models/weapons/g_boom/tris.md2", 13, 4, 6, 36, 39, p_ion, f_ion, false),
  D("phalanx", "q2:weapon_phalanx", "weapon_phalanx", "q2:ammo_magslug", 1, 5, "models/weapons/v_shotx/tris.md2", "models/weapons/g_shotx/tris.md2", 12, 5, 20, 58, 63, p_phalanx, f_phalanx, false),
};
const size_t qa_q2mp_xatrix_weapon_definition_count = N(qa_q2mp_xatrix_weapon_definitions);

const qa_q2w_definition qa_q2mp_rogue_weapon_definitions[] = {
  D("tesla", "q2:ammo_tesla", "ammo_tesla", "q2:ammo_tesla", 1, 2, "models/weapons/v_tesla/tris.md2", "models/ammo/am_tesl/tris.md2", 0, 0, 8, 32, 32, p_tesla, f_tesla, false),
  D("proxlauncher", "q2:weapon_proxlauncher", "weapon_proxlauncher", "q2:ammo_prox", 1, 5, "models/weapons/v_plaunch/tris.md2", "models/weapons/g_plaunch/tris.md2", 15, 5, 16, 59, 64, p_prox, f_prox, false),
  {"chainfist", "q2:weapon_chainfist", "weapon_chainfist", NULL, 0, 0, "models/weapons/v_chainf/tris.md2", "models/weapons/g_chainf/tris.md2", 16, 4, 32, 57, 60, NULL, 0, f_chainfist, N(f_chainfist), false, true},
  D("disintegrator", "q2:weapon_disintegrator", "weapon_disintegrator", "q2:ammo_disruptor", 1, 5, "models/weapons/v_dist/tris.md2", "models/weapons/g_dist/tris.md2", 12, 4, 9, 29, 34, p_dis, f_dis, false),
  D("etf_rifle", "q2:weapon_etf_rifle", "weapon_etf_rifle", "q2:ammo_flechettes", 1, 30, "models/weapons/v_etf_rifle/tris.md2", "models/weapons/g_etf_rifle/tris.md2", 13, 4, 7, 37, 41, p_etf, f_etf, false),
  D("heatbeam", "q2:weapon_plasmabeam", "weapon_plasmabeam", "q2:ammo_cells", 2, 10, "models/weapons/v_beamer/tris.md2", "models/weapons/g_beamer/tris.md2", 14, 8, 12, 39, 44, p_heat, f_heat, false),
};
const size_t qa_q2mp_rogue_weapon_definition_count = N(qa_q2mp_rogue_weapon_definitions);
