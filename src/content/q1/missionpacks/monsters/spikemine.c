/* Port of src/content/q1/missionpacks/monsters/spikemine.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/spikemine.h"

#include <stdio.h>
#include <string.h>

#include "monsters_internal.h"

static void spikemine_spawn(void *ctx, qa_q1mpm_game *game, qa_q1mpm_actor *entity) {
  (void)ctx;
  if (game->options.deathmatch != 0) { game->remove(game, entity); return; }
  entity->solid = "slidebox";
  entity->movement = "step";
  snprintf(entity->model, sizeof(entity->model), "%s", "progs/demon.mdl");
  qa_bounds bounds = {{-32, -32, -24}, {32, 32, 64}};
  game->set_bounds(game, entity, bounds);
  game->link(game, entity);
}
void qa_q1mpm_register_dormant_spikemine(qa_q1mpm_game *game) {
  game->register_spawn(game, "monster_spikemine", spikemine_spawn, game);
}
