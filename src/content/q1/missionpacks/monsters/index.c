/* Port of src/content/q1/missionpacks/monsters/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters.h"

#include <stdlib.h>

#include "monsters_internal.h"

qa_q1mpm_runtime *qa_q1mpm_register_mission_pack_monsters(qa_q1_services *game, qa_q1_base *base,
                                                          const char *pack,
                                                          const qa_q1mpm_mission_monster_hooks *hooks,
                                                          qa_q1mpm_runtime *out) {
  qa_q1mpm_runtime_init(out, game, base, pack, hooks);
  if (strcmp(pack, "hipnotic") == 0) {
    out->owned_defs = calloc(4, sizeof *out->owned_defs);
    if (out->owned_defs == NULL) qa_range_error("out of memory");
    out->owned_def_count = 4;
    qa_q1mpm_scourge_definition(out, &out->owned_defs[0]);
    qa_q1mpm_runtime_register(out, &out->owned_defs[0]);
    qa_q1mpm_gremlin_definition(out, &out->owned_defs[1]);
    qa_q1mpm_runtime_register(out, &out->owned_defs[1]);
    qa_q1mpm_armagon_definition(out, &out->owned_defs[2]);
    qa_q1mpm_runtime_register(out, &out->owned_defs[2]);
    qa_q1mpm_runtime_register(out, &qa_q1mpm_decoy_definition);
    qa_q1mpm_runtime_register(out, &qa_q1mpm_hipnotic_army_definition);
    qa_q1mpm_hipnotic_dog_definition(out, &out->owned_defs[3]);
    qa_q1mpm_runtime_register(out, &out->owned_defs[3]);
    qa_q1mpm_register_dormant_spikemine(game);
    qa_q1mpm_register_hipnotic_paths(out);
  }
  if (strcmp(pack, "rogue") == 0) {
    qa_q1mpm_register_overlord_destination(game);
    qa_q1mpm_register_dragon_corners(game);
    /* TS builds the list (morphDefinition/dragonDefinition run) before registering. */
    out->owned_defs = calloc(2, sizeof *out->owned_defs);
    if (out->owned_defs == NULL) qa_range_error("out of memory");
    out->owned_def_count = 2;
    qa_q1mpm_morph_definition(out, &out->owned_defs[0]);
    qa_q1mpm_dragon_definition(out, &out->owned_defs[1]);
    const qa_q1mpm_pack_monster_definition *const definitions[] = {
        &qa_q1mpm_eel_definition, &qa_q1mpm_sword_definition, &qa_q1mpm_wrath_definition,
        &qa_q1mpm_mummy_definition, &qa_q1mpm_lavaman_definition, &qa_q1mpm_overlord_definition,
        &out->owned_defs[0], &out->owned_defs[1]};
    for (size_t i = 0; i < sizeof definitions / sizeof definitions[0]; i++)
      qa_q1mpm_runtime_register(out, definitions[i]);
  }
  return out;
}
