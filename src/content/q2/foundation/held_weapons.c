/* Port of src/content/q2/foundation/held-weapons.ts. GPL-2.0-or-later.
 * Weapon viewModel/item rows transcribed from TS owners
 * foundation/weapons/definitions.ts and missionpacks/weapons/definitions.ts. */
#include "qa/content/q2/foundation/held_weapons.h"

#include <stdio.h>
#include <string.h>

static const qa_model_transform GRIP = {
  {-2.5316378672917685, -9.27009121576945, 4.795252025127411},
  {{{0.9701912999153137, -0.16778743267059326, -0.17486034333705902},
    {0.16538193821907043, 0.9858221411705017, -0.02834496460855007},
    {0.17713716626167297, -0.0014186727348715067, 0.9841850996017456}}},
  {1, 1, 1}};

typedef struct row { qa_item_id item; const char *view_model; const char *native; } row;
static const row ROWS[] = {
  {"q2:weapon_blaster", "models/weapons/v_blast/tris.md2", "w_blaster"},
  {"q2:weapon_shotgun", "models/weapons/v_shotg/tris.md2", "w_shotgun"},
  {"q2:weapon_supershotgun", "models/weapons/v_shotg2/tris.md2", "w_sshotgun"},
  {"q2:weapon_machinegun", "models/weapons/v_machn/tris.md2", "w_machinegun"},
  {"q2:weapon_chaingun", "models/weapons/v_chain/tris.md2", "w_chaingun"},
  {"q2:ammo_grenades", "models/weapons/v_handgr/tris.md2", "a_grenades"},
  {"q2:weapon_grenadelauncher", "models/weapons/v_launch/tris.md2", "w_glauncher"},
  {"q2:weapon_rocketlauncher", "models/weapons/v_rocket/tris.md2", "w_rlauncher"},
  {"q2:weapon_hyperblaster", "models/weapons/v_hyperb/tris.md2", "w_hyperblaster"},
  {"q2:weapon_railgun", "models/weapons/v_rail/tris.md2", "w_railgun"},
  {"q2:weapon_bfg", "models/weapons/v_bfg/tris.md2", "w_bfg"},
  {"q2:ammo_trap", "models/weapons/v_trap/tris.md2", "a_trap"},
  {"q2:weapon_boomer", "models/weapons/v_boomer/tris.md2", "w_ripper"},
  {"q2:weapon_phalanx", "models/weapons/v_shotx/tris.md2", "w_phalanx"},
  {"q2:ammo_tesla", "models/weapons/v_tesla/tris.md2", "a_tesla"},
  {"q2:weapon_proxlauncher", "models/weapons/v_launch/tris.md2", "w_plauncher"},
  {"q2:weapon_chainfist", "models/weapons/v_chainf/tris.md2", "w_chainfist"},
  {"q2:weapon_disintegrator", "models/weapons/v_dist/tris.md2", "w_disrupt"},
  {"q2:weapon_etf_rifle", "models/weapons/v_etf_rifle/tris.md2", "w_etfrifle"},
  {"q2:weapon_plasmabeam", "models/weapons/v_beamer/tris.md2", "w_plasma"},
};

bool qa_q2_held_weapon(const char *view_model, qa_item_id item, qa_held_weapon_model *out, char *buf, size_t buf_len) {
  const row *found = NULL;
  for (size_t i = 0; i < sizeof ROWS / sizeof ROWS[0]; i++) {
    if (item == NULL ? strcmp(ROWS[i].view_model, view_model) == 0 : strcmp(ROWS[i].item, item) == 0) { found = &ROWS[i]; break; }
  }
  const char *name = found ? found->native
      : (item == NULL && strcmp(view_model, "models/weapons/grapple/tris.md2") == 0 ? "w_grapple" : NULL);
  /* TS: weapon found by viewModel but not in nativeModels -> null (all rows above are native). */
  if (!name) return false;
  snprintf(buf, buf_len, "players/male/%s.md2", name);
  *out = (qa_held_weapon_model){0};
  out->path = buf;
  out->fallback = "players/male/weapon.md2";
  out->reference_frame = 0;
  out->grip = GRIP;
  return true;
}
