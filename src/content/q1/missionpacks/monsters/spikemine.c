/* Port of src/content/q1/missionpacks/monsters/spikemine.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/monsters/spikemine.h"

#include "monsters_internal.h"

/* TS `(_game, entity) => ...` over the registering game (ctx). */
static void spikemine_spawn(void *ctx, qa_q1_services *source, qa_q1_actor *entity) {
  (void)source;
  qa_q1_services *game = ctx;
  if (game->options.deathmatch != 0) { qa_q1_remove(game, entity); return; }
  entity->solid = "slidebox";
  entity->movement = "step";
  q1mpm_set_text(&entity->model, "progs/demon.mdl");
  qa_bounds bounds = {{-32, -32, -24}, {32, 32, 64}};
  qa_q1_set_bounds(game, entity, bounds);
  qa_q1_link(game, entity);
}
void qa_q1mpm_register_dormant_spikemine(qa_q1_services *game) {
  qa_q1_register_spawn(game, "monster_spikemine", spikemine_spawn, game);
}
