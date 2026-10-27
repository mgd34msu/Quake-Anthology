/* Port of src/content/q1/missionpacks/world/index.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/index.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/campaign.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/hipnotic_hazards.h"
#include "qa/content/q1/missionpacks/world/hipnotic_misc.h"
#include "qa/content/q1/missionpacks/world/hipnotic_particles.h"
#include "qa/content/q1/missionpacks/world/hipnotic_rotate.h"
#include "qa/content/q1/missionpacks/world/hipnotic_spawn.h"
#include "qa/content/q1/missionpacks/world/hipnotic_train.h"
#include "qa/content/q1/missionpacks/world/hipnotic_triggers.h"
#include "qa/content/q1/missionpacks/world/rogue_ending.h"
#include "qa/content/q1/missionpacks/world/rogue_hazards.h"
#include "qa/content/q1/missionpacks/world/rogue_misc.h"
#include "qa/content/q1/missionpacks/world/rogue_pendulum.h"
#include "qa/content/q1/missionpacks/world/rogue_plats.h"
#include "qa/content/q1/missionpacks/world/rogue_time.h"
#include "qa/content/q1/missionpacks/world/shooters.h"
#include <string.h>

static double zero_gamecfg(void *ctx) { (void)ctx; return 0; }
void qa_q1mp_missionpack_world_init(qa_q1mp_missionpack_world *world, qa_q1mp_game *game, qa_q1mp_pack pack, const qa_q1mp_world_hooks *hooks) {
  world->game = game; world->pack = pack;
  if (hooks) world->hooks = *hooks; else memset(&world->hooks, 0, sizeof world->hooks);
  world->runes = NULL; world->teams = NULL; world->tag = NULL;
  qa_q1mp_register_mission_shooters(game, pack);
  qa_q1mp_register_mission_campaign(game, pack, &world->hooks);
  if (qa_q1mp_streq(pack, "hipnotic")) {
    qa_q1mp_register_hipnotic_triggers(game);
    qa_q1mp_register_hipnotic_train(game);
    qa_q1mp_register_hipnotic_rotation(game);
    qa_q1mp_register_hipnotic_misc(game);
    qa_q1mp_register_hipnotic_particles(game);
    qa_q1mp_register_hipnotic_spawn(game, &world->hooks);
    qa_q1mp_register_hipnotic_hazards(game);
  } else {
    qa_q1mp_register_rogue_misc(game);
    qa_q1mp_register_rogue_time(game);
    qa_q1mp_register_rogue_ending(game);
    qa_q1mp_register_rogue_pendulum(game);
    qa_q1mp_register_rogue_plats(game);
    qa_q1mp_register_rogue_hazards(game);
    qa_q1mp_rogue_runes_init(&world->runes_storage, game, world->hooks.gamecfg ? world->hooks.gamecfg : zero_gamecfg, world->hooks.ctx);
    world->runes = &world->runes_storage;
    qa_q1mp_rogue_teams_init(&world->teams_storage, game, &world->hooks);
    world->teams = &world->teams_storage;
    qa_q1mp_rogue_tag_init(&world->tag_storage, game, &world->hooks);
    world->tag = &world->tag_storage;
  }
}
qa_q1mp_missionpack_world *qa_q1mp_register_missionpack_world(qa_q1mp_game *game, qa_q1mp_pack pack, const qa_q1mp_world_hooks *hooks, qa_q1mp_missionpack_world *out) {
  qa_q1mp_missionpack_world_init(out, game, pack, hooks); return out;
}
void qa_q1mp_world_after_physics(qa_q1mp_missionpack_world *world, qa_actor_id actor, double seconds) {
  (void)seconds;
  if (qa_q1mp_streq(world->pack, "hipnotic")) { qa_q1mp_earthquake_after_physics(world->game, actor); return; }
  qa_q1mp_actor *w = world->game->base->world;
  if (w && qa_q1mp_number(w, "rogue:earthquake_active") == 1)
    qa_q1mp_rogue_earthquake(world->game, actor, qa_q1mp_number(w, "rogue:earthquake_intensity"));
  if (world->teams) qa_q1mp_teams_frame(world->teams, actor);
  if (world->runes) qa_q1mp_runes_frame(world->runes, actor);
  qa_q1mp_start_rogue_ending(world->game, actor, &world->hooks);
}
void qa_q1mp_world_player_spawned(qa_q1mp_missionpack_world *world, qa_actor_id actor) { if (world->teams) qa_q1mp_teams_player_spawned(world->teams, actor); }
void qa_q1mp_world_drop_carried_flag(qa_q1mp_missionpack_world *world, qa_actor_id actor) { if (world->teams) qa_q1mp_teams_drop_carried_flag(world->teams, actor); }
bool qa_q1mp_world_impulse(qa_q1mp_missionpack_world *world, qa_actor_id actor, double impulse) { return world->teams ? qa_q1mp_teams_impulse(world->teams, actor, impulse) : false; }
double qa_q1mp_world_saved_team(qa_q1mp_missionpack_world *world, qa_actor_id actor) { return world->teams ? qa_q1mp_teams_team(world->teams, actor) : 0; }
qa_q1mp_actor *qa_q1mp_world_select_spawn(qa_q1mp_missionpack_world *world, qa_actor_id actor) { return world->teams ? qa_q1mp_teams_select_spawn(world->teams, actor) : NULL; }
double qa_q1mp_world_tag_score(qa_q1mp_missionpack_world *world, qa_actor_id victim, qa_actor_id attacker) { return world->tag ? qa_q1mp_tag_score(world->tag, victim, attacker) : 1; }
void qa_q1mp_world_confirmed_damage(qa_q1mp_missionpack_world *world, qa_actor_id target, qa_q1mp_opt_actor attacker) { if (world->teams) qa_q1mp_teams_confirmed_damage(world->teams, target, attacker); }
void qa_q1mp_world_player_died(qa_q1mp_missionpack_world *world, qa_actor_id actor, qa_q1mp_opt_actor attacker) {
  if (world->teams) qa_q1mp_teams_player_died(world->teams, actor, attacker);
  if (world->runes) qa_q1mp_runes_drop(world->runes, actor);
}
double qa_q1mp_world_rune_attack_delay(qa_q1mp_missionpack_world *world, qa_actor_id actor, double delay) { return world->runes ? qa_q1mp_runes_attack_delay(world->runes, actor, delay) : delay; }
void qa_q1mp_world_rune_attack_sound(qa_q1mp_missionpack_world *world, qa_actor_id actor) { if (world->runes) qa_q1mp_runes_attack_sound(world->runes, actor); }
double qa_q1mp_world_rune_damage(qa_q1mp_missionpack_world *world, qa_actor_id actor, double amount) { return world->runes ? qa_q1mp_runes_damage(world->runes, actor, amount) : amount; }
double qa_q1mp_world_rune_resistance(qa_q1mp_missionpack_world *world, qa_actor_id actor, double amount) { return world->runes ? qa_q1mp_runes_resistance(world->runes, actor, amount) : amount; }
bool qa_q1mp_world_has_regeneration_rune(qa_q1mp_missionpack_world *world, qa_actor_id actor) { return world->runes ? qa_q1mp_runes_has_regeneration(world->runes, actor) : false; }
void qa_q1mp_world_crash_time_machine(qa_q1mp_missionpack_world *world) { qa_q1mp_crash_time_machine(world->game); }
