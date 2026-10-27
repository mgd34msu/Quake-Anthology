/* Port of src/content/q1/missionpacks/monsters/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters.h"

#include <stdlib.h>

#include "monsters_internal.h"

bool qa_q1mpm_register_mission_pack_monsters(qa_q1mpm_game *game, qa_q1mpm_q1base *base,
                                             const char *pack,
                                             const qa_q1mpm_mission_monster_hooks *hooks,
                                             qa_q1mpm_runtime *out) {
  static const qa_q1mpm_mission_monster_hooks no_hooks = {NULL, NULL};
  if (!qa_q1mpm_runtime_init(out, game, base, pack, hooks == NULL ? &no_hooks : hooks)) return false;
  if (qa_q1mpm_str_eq(pack, "hipnotic")) {
    out->owned_defs = calloc(4, sizeof(*out->owned_defs));
    if (out->owned_defs == NULL) return false;
    out->owned_def_count = 4;
    qa_q1mpm_scourge_definition(out, &out->owned_defs[0]);
    if (!qa_q1mpm_runtime_register(out, &out->owned_defs[0])) return false;
    qa_q1mpm_gremlin_definition(out, &out->owned_defs[1]);
    if (!qa_q1mpm_runtime_register(out, &out->owned_defs[1])) return false;
    qa_q1mpm_armagon_definition(out, &out->owned_defs[2]);
    if (!qa_q1mpm_runtime_register(out, &out->owned_defs[2])) return false;
    if (!qa_q1mpm_runtime_register(out, &qa_q1mpm_decoy_definition)) return false;
    if (!qa_q1mpm_runtime_register(out, &qa_q1mpm_hipnotic_army_definition)) return false;
    qa_q1mpm_hipnotic_dog_definition(out, &out->owned_defs[3]);
    if (!qa_q1mpm_runtime_register(out, &out->owned_defs[3])) return false;
    qa_q1mpm_register_dormant_spikemine(game);
    qa_q1mpm_register_hipnotic_paths(out);
  }
  if (qa_q1mpm_str_eq(pack, "rogue")) {
    out->owned_defs = calloc(2, sizeof(*out->owned_defs));
    if (out->owned_defs == NULL) return false;
    out->owned_def_count = 2;
    qa_q1mpm_register_overlord_destination(game);
    qa_q1mpm_register_dragon_corners(game);
    const qa_q1mpm_pack_monster_definition *const fixed[] = {
        &qa_q1mpm_eel_definition, &qa_q1mpm_sword_definition, &qa_q1mpm_wrath_definition,
        &qa_q1mpm_mummy_definition, &qa_q1mpm_lavaman_definition, &qa_q1mpm_overlord_definition};
    for (size_t i = 0; i < sizeof(fixed) / sizeof(fixed[0]); i++)
      if (!qa_q1mpm_runtime_register(out, fixed[i])) return false;
    qa_q1mpm_morph_definition(out, &out->owned_defs[0]);
    if (!qa_q1mpm_runtime_register(out, &out->owned_defs[0])) return false;
    qa_q1mpm_dragon_definition(out, &out->owned_defs[1]);
    if (!qa_q1mpm_runtime_register(out, &out->owned_defs[1])) return false;
  }
  return true;
}
