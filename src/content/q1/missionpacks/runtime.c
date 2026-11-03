/* Port of src/content/q1/missionpacks/runtime.ts. The world receives the
 * runtime's own charmer/charm/becomeDecoy over the caller's options; every
 * other hook forwards to the caller's leaf with the caller's ctx.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/runtime.h"

#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/backpacks.h"
#include "qa/content/q1/missionpacks/monsters.h"

/* TS () => this.arsenal.hornCharmer (monsters and world). */
static bool horn_charmer(void *ctx, qa_actor_id *out) {
  const qa_q1mp_mission_pack_runtime *runtime = ctx;
  return qa_q1mp_arsenal_horn_charmer(&runtime->arsenal, out);
}
static void charm(void *ctx, qa_q1_actor *entity, qa_actor_id charmer) {
  qa_q1mp_mission_pack_runtime *runtime = ctx;
  qa_q1mpm_runtime_charm(runtime->monsters, entity, charmer);
}
static qa_q1_actor *become_decoy(void *ctx, const char *target, qa_vec3 origin) {
  qa_q1mp_mission_pack_runtime *runtime = ctx;
  return qa_q1mpm_become_decoy(runtime->monsters, target, origin);
}
static void present_finale(void *ctx, const qa_q1_intermission_result *result) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  hooks->present_finale(hooks->ctx, result);
}
static double gamecfg(void *ctx) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  return hooks->gamecfg(hooks->ctx);
}
static double team_color(void *ctx, qa_actor_id actor) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  return hooks->team_color(hooks->ctx, actor);
}
static void set_team_color(void *ctx, qa_actor_id actor, double team) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  hooks->set_team_color(hooks->ctx, actor, team);
}
static void add_frags(void *ctx, qa_actor_id actor, double delta) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  hooks->add_frags(hooks->ctx, actor, delta);
}
static double frags(void *ctx, qa_actor_id actor) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  return hooks->frags(hooks->ctx, actor);
}
static void disconnect(void *ctx, qa_actor_id actor) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  hooks->disconnect(hooks->ctx, actor);
}
static double player_frame(void *ctx, qa_actor_id actor) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  return hooks->player_frame(hooks->ctx, actor);
}
static const char *player_name(void *ctx, qa_actor_id actor) {
  const qa_q1mp_world_hooks *hooks = &((qa_q1mp_mission_pack_runtime *)ctx)->options.world;
  return hooks->player_name(hooks->ctx, actor);
}
static bool no_footsteps(void *ctx) { (void)ctx; return false; } /* TS options.footsteps ?? (() => false) */
static void world_postthink(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player, double seconds) {
  (void)runtime; /* TS (_runtime, player, seconds) */
  qa_q1mp_mission_pack_runtime *self = ctx;
  qa_q1mp_world_after_physics(&self->world, player->actor.id, seconds);
}
static void coordinate_dump(void *ctx, qa_q1_services *runtime, qa_q1_player_state *player, double seconds) {
  (void)ctx; (void)seconds; /* TS (runtime, player) */
  qa_q1mp_dump_mission_pack_coordinates(runtime, player);
}

void qa_q1mp_mission_pack_runtime_init(qa_q1mp_mission_pack_runtime *runtime, qa_q1_services *game, qa_q1_base *base,
                                       qa_q1mp_pack pack, const qa_q1mp_mission_pack_options *options) {
  memset(runtime, 0, sizeof *runtime);
  runtime->game = game; runtime->base = base; runtime->pack = pack;
  if (options != NULL) runtime->options = *options;
  qa_q1mp_mission_pack_arsenal_init(&runtime->arsenal, game, pack);
  if (runtime->options.footsteps != NULL)
    qa_q1mp_character_effects_init(&runtime->character_effects, game, pack, runtime->options.footsteps, runtime->options.footsteps_ctx);
  else qa_q1mp_character_effects_init(&runtime->character_effects, game, pack, no_footsteps, NULL);
  runtime->monsters = malloc(sizeof *runtime->monsters);
  if (runtime->monsters == NULL) qa_range_error("Out of memory");
  const qa_q1mpm_mission_monster_hooks monster_hooks = {.ctx = runtime, .charmer = horn_charmer};
  (void)qa_q1mpm_register_mission_pack_monsters(game, base, pack, &monster_hooks, runtime->monsters);
  const qa_q1mp_world_hooks *caller = &runtime->options.world;
  qa_q1mp_world_hooks hooks;
  memset(&hooks, 0, sizeof hooks);
  hooks.ctx = runtime;
  hooks.charmer = horn_charmer; hooks.charm = charm; hooks.become_decoy = become_decoy;
  hooks.present_finale = caller->present_finale != NULL ? present_finale : NULL;
  hooks.gamecfg = caller->gamecfg != NULL ? gamecfg : NULL;
  hooks.team_color = caller->team_color != NULL ? team_color : NULL;
  hooks.set_team_color = caller->set_team_color != NULL ? set_team_color : NULL;
  hooks.add_frags = caller->add_frags != NULL ? add_frags : NULL;
  hooks.frags = caller->frags != NULL ? frags : NULL;
  hooks.disconnect = caller->disconnect != NULL ? disconnect : NULL;
  hooks.player_frame = caller->player_frame != NULL ? player_frame : NULL;
  hooks.player_name = caller->player_name != NULL ? player_name : NULL;
  qa_q1mp_missionpack_world_init(&runtime->world, game, pack, &hooks);
  const bool hipnotic = qa_q1mp_streq(pack, "hipnotic");
  runtime->postthink.ctx = runtime;
  runtime->postthink.id = hipnotic ? "q1:hipnotic:world-postthink" : "q1:rogue:world-postthink";
  runtime->postthink.after_physics = world_postthink;
  qa_q1_register_player_extension(game, &runtime->postthink);
  if (hipnotic) {
    runtime->coordinate_dump.ctx = runtime;
    runtime->coordinate_dump.id = "q1:hipnotic:coordinate-dump";
    runtime->coordinate_dump.frame = coordinate_dump;
    qa_q1_register_player_extension(game, &runtime->coordinate_dump);
  }
}
void qa_q1mp_mission_pack_runtime_destroy(qa_q1mp_mission_pack_runtime *runtime) {
  qa_q1mp_missionpack_world_destroy(&runtime->world);
  if (runtime->monsters != NULL) { qa_q1mpm_runtime_destroy(runtime->monsters); free(runtime->monsters); runtime->monsters = NULL; }
  qa_q1mp_mission_pack_arsenal_destroy(&runtime->arsenal);
}
qa_q1mp_mission_pack_runtime *qa_q1mp_register_q1_mission_pack(qa_q1_services *game, qa_q1_base *base, qa_q1mp_pack pack,
                                                               const qa_q1mp_mission_pack_options *options,
                                                               qa_q1mp_mission_pack_runtime *out) {
  qa_q1mp_mission_pack_runtime_init(out, game, base, pack, options);
  return out;
}
bool qa_q1mp_runtime_impulse(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, double impulse) {
  qa_q1_player_state *player = qa_q1_player(runtime->game, actor);
  if (player == NULL) return false;
  return qa_q1mp_mission_pack_command(runtime->game, runtime->base, &runtime->arsenal.players, player, runtime->pack, impulse,
                                      &runtime->options.command) ||
         qa_q1mp_arsenal_impulse(&runtime->arsenal, actor, impulse) || qa_q1mp_world_impulse(&runtime->world, actor, impulse);
}
void qa_q1mp_runtime_player_spawned(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor) {
  qa_q1mp_world_player_spawned(&runtime->world, actor);
}
void qa_q1mp_runtime_confirmed_damage(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id target, const qa_actor_id *attacker) {
  qa_q1mp_world_confirmed_damage(&runtime->world, target, attacker);
}
void qa_q1mp_runtime_player_died(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, const qa_actor_id *attacker) {
  const qa_q1_actor *source = qa_q1_get_entity(runtime->game, attacker);
  if (qa_q1mp_streq(runtime->pack, "rogue") && source != NULL && qa_q1mp_streq(source->classname, "power_shield")) {
    qa_q1mp_world_player_died(&runtime->world, actor, source->has_owner ? &source->owner : NULL);
    return;
  }
  qa_q1mp_world_player_died(&runtime->world, actor, attacker);
}
qa_q1_travel_state qa_q1mp_runtime_new_travel(qa_q1mp_mission_pack_runtime *runtime) {
  return qa_q1mp_new_mission_pack_travel(runtime->game, runtime->pack);
}
qa_q1_travel_state qa_q1mp_runtime_capture_travel(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor) {
  return qa_q1mp_capture_mission_pack_travel(runtime->game, actor, runtime->pack);
}
qa_q1_travel_state qa_q1mp_runtime_decode_travel(qa_q1mp_mission_pack_runtime *runtime, const qa_q1_travel_state *state, double server_flags) {
  return qa_q1mp_decode_mission_pack_travel(runtime->game, state, server_flags, runtime->pack);
}
void qa_q1mp_runtime_admit_travel(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor, const qa_q1_travel_state *state) {
  qa_q1mp_admit_mission_pack_travel(runtime->game, actor, state, runtime->pack);
}
qa_q1_actor *qa_q1mp_runtime_drop_backpack(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor) {
  return qa_q1mp_drop_mission_pack_backpack(runtime->game, actor, runtime->pack);
}
qa_q1_character_source_pose qa_q1mp_runtime_character_pose(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor) {
  return qa_q1mp_mission_pack_character_pose(runtime->game, actor, runtime->pack);
}
void qa_q1mp_runtime_character_frame(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, const qa_q1_character_presentation *presentation) {
  qa_q1mp_character_effects_frame(&runtime->character_effects, actor, presentation);
}
bool qa_q1mp_runtime_suicide(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor) {
  return qa_q1mp_world_suicide(&runtime->world, actor);
}
qa_q1_actor *qa_q1mp_runtime_select_spawn(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor) {
  return qa_q1mp_world_select_spawn(&runtime->world, actor);
}
typedef struct tag_score_ctx { qa_q1mp_mission_pack_runtime *runtime; const qa_q1_obituary_input *input; } tag_score_ctx;
static double tag_score(void *ctx) {
  const tag_score_ctx *score = ctx;
  if (score->input->attacker == NULL) return 1;
  return qa_q1mp_world_tag_score(&score->runtime->world, score->input->victim.actor, score->input->attacker->actor);
}
qa_q1_obituary qa_q1mp_runtime_obituary(qa_q1mp_mission_pack_runtime *runtime, const qa_q1_obituary_input *input,
                                        const qa_actor_id *inflictor) {
  qa_q1_services *game = runtime->game;
  qa_q1mp_obituary_context context;
  memset(&context, 0, sizeof context);
  context.pack = runtime->pack;
  context.inflictor_classname = inflictor == NULL ? "" : game->host.classname(game->host.ctx, *inflictor);
  const qa_q1_actor *attacker = qa_q1_get_entity(game, input->attacker != NULL ? &input->attacker->actor : NULL);
  context.attacker_death_type = attacker != NULL ? qa_q1_actor_text(attacker, "deathtype") : "";
  context.victim_saved_team = qa_q1mp_world_saved_team(&runtime->world, input->victim.actor);
  context.gamecfg = runtime->options.world.gamecfg != NULL ? runtime->options.world.gamecfg(runtime->options.world.ctx) : 0;
  tag_score_ctx score = {runtime, input};
  context.tag_ctx = &score; context.tag_score = tag_score;
  return qa_q1mp_mission_pack_obituary(input, &context);
}
