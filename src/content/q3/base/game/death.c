/* Port of src/content/q3/base/game/death.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/death.h"
void qa_q3_return_persistent_powerup(qa_q3_game_entity *powerup, qa_q3_server_world_mirror world) {
  powerup->r.sv_flags = (int)powerup->r.sv_flags & ~1;
  powerup->s.e_flags = (int)powerup->s.e_flags & ~0x80;
  powerup->r.contents = (double)(int32_t)0x40000000;
  world.link(world.ctx, powerup);
}
void qa_q3_toss_client_persistent_powerup(qa_q3_game_entity *entity, qa_q3_return_item_fn return_item, void *return_ctx) {
  qa_q3_game_client *client = entity->client;
  if (!client || !client->persistant_powerup) return;
  return_item(return_ctx, client->persistant_powerup);
  qa_q3_game_slots_set_mirror(&client->ps.stats, 2, 0);
  client->persistant_powerup = NULL;
}
void qa_q3_death_runtime_init(qa_q3_death_runtime *runtime, qa_q3_death_host host) { runtime->host = host; }
