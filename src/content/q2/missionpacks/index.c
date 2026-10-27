/* Port of src/content/q2/missionpacks/index.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks.h"

#include <string.h>

void qa_q2mp_register_armory(qa_q2mp_pack pack, qa_q2mp_armory_hooks hooks, qa_q2f_edition edition, qa_q2mp_armory *out) {
  qa_q2mp_projectile_hooks projectile_hooks;
  memset(&projectile_hooks, 0, sizeof projectile_hooks);
  projectile_hooks.base = &hooks.weapons->base;
  projectile_hooks.strong_mines = hooks.strong_mines;
  projectile_hooks.gravity_ctx = hooks.gravity_ctx;
  projectile_hooks.gravity = hooks.gravity;
  projectile_hooks.ctx = hooks.ctx;
  projectile_hooks.monster = hooks.monster;
  projectile_hooks.player_effect = hooks.player_effect;
  qa_q2mp_projectiles_init(&out->projectiles, projectile_hooks);
  qa_q2mp_weapons_init(&out->weapons, &out->projectiles);
  qa_q2mp_sphere_hooks sphere_hooks = {hooks.hunter_camera, hooks.ctx, hooks.intermission, hooks.player_effect};
  qa_q2mp_spheres_init(&out->spheres, &out->projectiles, sphere_hooks);
  qa_q2mp_doppleganger_init(&out->doppleganger, &out->spheres, hooks.weapons);
  qa_q2mp_item_hooks item_hooks = {hooks.ctx, hooks.player_effect, hooks.print};
  qa_q2mp_items_init(&out->items, item_hooks, pack, hooks.items);
  qa_q2mp_pack packs[2] = {pack, pack};
  size_t pack_count = 1;
  if (edition == QA_Q2F_RERELEASE) {
    packs[0] = QA_Q2MP_PACK_XATRIX;
    packs[1] = QA_Q2MP_PACK_ROGUE;
    pack_count = 2;
  }
  for (size_t i = 0; i < pack_count; i++) {
    qa_q2mp_weapons_register(&out->weapons, hooks.weapons, packs[i], edition);
    qa_q2mp_items_register(&out->items, &hooks.items, hooks.weapons, packs[i], &out->projectiles, &out->spheres,
                           &out->doppleganger, edition);
  }
  if (edition == QA_Q2F_RERELEASE) {
    static const qa_q2w_name order[] = {"disintegrator", "railgun", "heatbeam", "ionripper", "hyperblaster",
                                        "etf_rifle", "chaingun", "machinegun", "supershotgun", "shotgun", "phalanx",
                                        "rocketlauncher", "grenadelauncher", "proxlauncher", "chainfist", "blaster"};
    qa_q2w_weapons_set_fallback_order(hooks.weapons, order, 16);
  }
}
void qa_q2mp_armory_destroy(qa_q2mp_armory *armory) { qa_q2mp_items_destroy(&armory->items); }
