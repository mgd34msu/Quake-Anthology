/* Port of src/content/q1/missionpacks/runtime.ts. Monsters
 * registration is injected (options.register_monsters) because the
 * landed monsters port uses the qa_q1mpm_* game mirror - see
 * runtime.h. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/runtime.h"
#include "missionpacks_internal.h"
#include "qa/content/q1/missionpacks/backpacks.h"
#include "qa/content/q1/missionpacks/commands.h"
#include "qa/content/q1/missionpacks/obituaries.h"
#include <stdlib.h>
#include <string.h>

typedef struct rt_ctx { qa_q1mp_mission_pack_runtime *runtime; qa_q1mp_world_hooks caller; } rt_ctx;
/* Caller-hook forwards: world hooks share one ctx (rt_ctx), so each
 * caller hook is re-invoked with the caller's own ctx. */
static void fwd_present_finale(void *v, const qa_q1mp_source_finale *r) { rt_ctx *c = v; if (c->caller.present_finale) c->caller.present_finale(c->caller.ctx, r); }
static double fwd_gamecfg(void *v) { rt_ctx *c = v; return c->caller.gamecfg ? c->caller.gamecfg(c->caller.ctx) : 0; }
static double fwd_team_color(void *v, qa_actor_id a) { rt_ctx *c = v; return c->caller.team_color ? c->caller.team_color(c->caller.ctx, a) : 0; }
static void fwd_set_team_color(void *v, qa_actor_id a, double t) { rt_ctx *c = v; if (c->caller.set_team_color) c->caller.set_team_color(c->caller.ctx, a, t); }
static void fwd_add_frags(void *v, qa_actor_id a, double d) { rt_ctx *c = v; if (c->caller.add_frags) c->caller.add_frags(c->caller.ctx, a, d); }
static double fwd_frags(void *v, qa_actor_id a) { rt_ctx *c = v; return c->caller.frags ? c->caller.frags(c->caller.ctx, a) : 0; }
static void fwd_disconnect(void *v, qa_actor_id a) { rt_ctx *c = v; if (c->caller.disconnect) c->caller.disconnect(c->caller.ctx, a); }
static double fwd_player_frame(void *v, qa_actor_id a) { rt_ctx *c = v; return c->caller.player_frame ? c->caller.player_frame(c->caller.ctx, a) : 0; }
static const char *fwd_player_name(void *v, qa_actor_id a) { rt_ctx *c = v; return c->caller.player_name ? c->caller.player_name(c->caller.ctx, a) : NULL; }
static bool footsteps_false(void *ctx) { (void)ctx; return false; }
static qa_q1mp_opt_actor charmer_hook(void *vctx) {
  return qa_q1mp_arsenal_horn_charmer(&((rt_ctx *)vctx)->runtime->arsenal);
}
static void charm_hook(void *vctx, qa_q1mp_actor *entity, qa_actor_id charmer) {
  qa_q1mp_mission_pack_runtime *r = ((rt_ctx *)vctx)->runtime;
  if (r->options.monsters_charm) r->options.monsters_charm(r->options.monsters_ctx, r->monsters, entity, charmer);
}
static qa_q1mp_actor *decoy_hook(void *vctx, const char *target, qa_vec3 origin) {
  qa_q1mp_mission_pack_runtime *r = ((rt_ctx *)vctx)->runtime;
  return r->options.become_decoy ? r->options.become_decoy(r->options.monsters_ctx, r->monsters, target, origin) : NULL;
}
static void after_physics_cb(void *vctx, qa_q1mp_game *g, qa_q1mp_player_state *player, double seconds) {
  (void)g; qa_q1mp_mission_pack_runtime *r = ((rt_ctx *)vctx)->runtime;
  qa_q1mp_world_after_physics(&r->world, player->actor.id, seconds);
}
static void coord_frame_cb(void *vctx, qa_q1mp_game *g, qa_q1mp_player_state *player) {
  (void)vctx; qa_q1mp_dump_mission_pack_coordinates(g, player);
}
void qa_q1mp_mission_pack_runtime_init(qa_q1mp_mission_pack_runtime *runtime, qa_q1mp_game *game, qa_q1mp_q1base *base, qa_q1mp_pack pack, const qa_q1mp_mission_pack_options *options) {
  runtime->game = game; runtime->base = base; runtime->pack = pack;
  if (options) runtime->options = *options; else memset(&runtime->options, 0, sizeof runtime->options);
  runtime->monsters = runtime->options.monsters;
  qa_q1mp_mission_pack_arsenal_init(&runtime->arsenal, game, pack);
  qa_q1mp_character_effects_init(&runtime->character_effects, game, pack,
    runtime->options.footsteps ? runtime->options.footsteps : footsteps_false, runtime->options.footsteps_ctx);
  rt_ctx *hook = malloc(sizeof *hook); hook->runtime = runtime; hook->caller = runtime->options.world;
  if (runtime->options.register_monsters)
    runtime->monsters = runtime->options.register_monsters(runtime->options.monsters_ctx, game, base, pack, &runtime->arsenal);
  qa_q1mp_world_hooks world_hooks = {0};
  world_hooks.ctx = hook; world_hooks.charmer = charmer_hook; world_hooks.charm = charm_hook; world_hooks.become_decoy = decoy_hook;
  if (hook->caller.present_finale) world_hooks.present_finale = fwd_present_finale;
  if (hook->caller.gamecfg) world_hooks.gamecfg = fwd_gamecfg;
  if (hook->caller.team_color) world_hooks.team_color = fwd_team_color;
  if (hook->caller.set_team_color) world_hooks.set_team_color = fwd_set_team_color;
  if (hook->caller.add_frags) world_hooks.add_frags = fwd_add_frags;
  if (hook->caller.frags) world_hooks.frags = fwd_frags;
  if (hook->caller.disconnect) world_hooks.disconnect = fwd_disconnect;
  if (hook->caller.player_frame) world_hooks.player_frame = fwd_player_frame;
  if (hook->caller.player_name) world_hooks.player_name = fwd_player_name;
  qa_q1mp_missionpack_world_init(&runtime->world, game, pack, &world_hooks);
  if (game->register_player_extension) {
    static char idbuf[64]; (void)snprintf(idbuf, sizeof idbuf, "q1:%s:world-postthink", pack);
    qa_q1mp_player_extension ext = {0}; ext.id = idbuf; ext.ctx = hook; ext.after_physics = after_physics_cb;
    game->register_player_extension(game->ctx, &ext);
    if (qa_q1mp_streq(pack, "hipnotic")) {
      qa_q1mp_player_extension ext2 = {0}; ext2.id = "q1:hipnotic:coordinate-dump"; ext2.ctx = hook; ext2.frame = coord_frame_cb;
      game->register_player_extension(game->ctx, &ext2);
    }
  }
}
qa_q1mp_mission_pack_runtime *qa_q1mp_register_q1_mission_pack(qa_q1mp_game *game, qa_q1mp_q1base *base, qa_q1mp_pack pack, const qa_q1mp_mission_pack_options *options, qa_q1mp_mission_pack_runtime *out) {
  qa_q1mp_mission_pack_runtime_init(out, game, base, pack, options); return out;
}
bool qa_q1mp_runtime_impulse(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, double impulse) {
  qa_q1mp_player_state *player = runtime->game->base->player(runtime->game->base, actor);
  if (!player) return false;
  return qa_q1mp_mission_pack_command(runtime->game, runtime->base, &runtime->arsenal.players, player, runtime->pack, impulse, &runtime->options.command)
      || qa_q1mp_arsenal_impulse(&runtime->arsenal, actor, impulse)
      || qa_q1mp_world_impulse(&runtime->world, actor, impulse);
}
void qa_q1mp_runtime_player_spawned(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor) { qa_q1mp_world_player_spawned(&runtime->world, actor); }
void qa_q1mp_runtime_confirmed_damage(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id target, qa_q1mp_opt_actor attacker) { qa_q1mp_world_confirmed_damage(&runtime->world, target, attacker); }
void qa_q1mp_runtime_player_died(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, qa_q1mp_opt_actor attacker) {
  qa_q1mp_actor *source = attacker.has ? runtime->game->base->entity(runtime->game->base, attacker) : NULL;
  qa_q1mp_opt_actor effective = (qa_q1mp_streq(runtime->pack,"rogue") && source && qa_q1mp_streq(source->classname,"power_shield")) ? source->owner : attacker;
  qa_q1mp_world_player_died(&runtime->world, actor, effective);
}
qa_q1mp_travel_state qa_q1mp_runtime_new_travel(qa_q1mp_mission_pack_runtime *runtime) { return qa_q1mp_new_mission_pack_travel(runtime->game, runtime->pack); }
qa_q1mp_travel_state qa_q1mp_runtime_capture_travel(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor) { return qa_q1mp_capture_mission_pack_travel(runtime->game, actor, runtime->pack); }
qa_q1mp_travel_state qa_q1mp_runtime_decode_travel(qa_q1mp_mission_pack_runtime *runtime, qa_q1mp_travel_state state, double server_flags) { return qa_q1mp_decode_mission_pack_travel(runtime->game, state, server_flags, runtime->pack); }
void qa_q1mp_runtime_admit_travel(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor, qa_q1mp_travel_state state) { qa_q1mp_admit_mission_pack_travel(runtime->game, actor, state, runtime->pack); }
qa_q1mp_actor *qa_q1mp_runtime_drop_backpack(qa_q1mp_mission_pack_runtime *runtime, qa_owned_actor actor) { return qa_q1mp_drop_mission_pack_backpack(runtime->game, actor, runtime->pack); }
qa_q1mp_character_source_pose qa_q1mp_runtime_character_pose(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor) { return qa_q1mp_mission_pack_character_pose(runtime->game, actor, runtime->pack); }
void qa_q1mp_runtime_character_frame(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor, const qa_q1mp_character_presentation *presentation) { qa_q1mp_character_effects_frame(&runtime->character_effects, actor, presentation); }
qa_q1mp_actor *qa_q1mp_runtime_select_spawn(qa_q1mp_mission_pack_runtime *runtime, qa_actor_id actor) { return qa_q1mp_world_select_spawn(&runtime->world, actor); }

typedef struct tag_ctx2 { qa_q1mp_mission_pack_runtime *runtime; qa_actor_id victim; qa_actor_id attacker; } tag_ctx2;
static double tag_score_cb(void *vctx) {
  tag_ctx2 *c = vctx;
  return qa_q1mp_world_tag_score(&c->runtime->world, c->victim, c->attacker);
}
qa_q1mp_obituary qa_q1mp_runtime_obituary(qa_q1mp_mission_pack_runtime *runtime, const qa_q1mp_obituary_input *input, qa_q1mp_opt_actor inflictor) {
  tag_ctx2 tc = {runtime, input->victim.actor, input->has_attacker ? input->attacker.actor : input->victim.actor};
  qa_q1mp_actor *attacker_entity = (input->has_attacker) ? runtime->game->base->entity(runtime->game->base, QA_Q1MP_SOME(input->attacker.actor)) : NULL;
  qa_q1mp_obituary_context context = {0};
  context.pack = runtime->pack;
  context.inflictor_classname = inflictor.has ? runtime->game->base->host->classname(runtime->game->base->host->ctx, inflictor.id) : "";
  context.attacker_death_type = attacker_entity ? qa_q1mp_text(attacker_entity, "deathtype") : "";
  context.victim_saved_team = qa_q1mp_world_saved_team(&runtime->world, input->victim.actor);
  context.gamecfg = runtime->options.world.gamecfg ? runtime->options.world.gamecfg(runtime->options.world.ctx) : 0;
  context.tag_ctx = &tc;
  context.tag_score = input->has_attacker ? tag_score_cb : NULL;
  /* TS tagScore: () => attacker === null ? 1 : world.tagScore(...) -
   * when attacker is null the callback itself returns 1. */
  if (!input->has_attacker) { context.tag_score = NULL; }
  return qa_q1mp_mission_pack_obituary(input, &context);
}
