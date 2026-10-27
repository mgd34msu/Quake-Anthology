/* Port of src/content/q1/missionpacks/world/rogue-ending.ts.
 * spawnTeleportFog/createMissile: fog via hipnotic_spawn ext pattern,
 * missile via landed projectiles. PLAYER_BOUNDS mirrored from
 * foundation/types.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_ending.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/missionpacks/types.h"
#include "qa/content/q1/missionpacks/world/campaign.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/content/q1/missionpacks/world/finale_text.h"
#include "qa/content/q1/missionpacks/world/index.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>

qa_q1mp_actor *qa_q1mp_ext_spawn_teleport_fog(qa_q1mp_game *game, qa_vec3 origin);
typedef struct re_ctx { qa_q1mp_game *game; qa_q1mp_world_hooks hooks; } re_ctx;
static re_ctx *ending_ctx;
static const qa_bounds PLAYER_BOUNDS = {{-16,-16,-24},{16,16,32}};
static qa_q1mp_actor *machine_of(qa_q1mp_game *game) {
  qa_q1b_game *g = game->base;
  qa_q1mp_opt_actor ref = g->world ? qa_q1b_references_get(g->world, "rogue:theMachine") : QA_Q1MP_NONE;
  qa_q1mp_actor *entity = ref.has ? g->entity(g, ref) : NULL;
  if (!entity) qa_range_error("End sequence time machine is missing");
  return entity;
}
static void remove_stuff(re_ctx *c) {
  qa_q1b_game *g = c->game->base; qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512);
  qa_q1mp_actor *core = NULL;
  for (size_t i = 0; i < n; i++) {
    if (qa_q1mp_streq(ents[i]->classname, "ltrail_start")) g->remove(g, ents[i]);
    if (!core && qa_q1mp_streq(ents[i]->classname, "item_time_core")) core = ents[i];
  }
  if (c->game->emit_colored_explosion) c->game->emit_colored_explosion(c->game->ctx, core ? g->body(g, core).origin : QA_Q1MP_ZERO, 230, 5);
  if (core) qa_q1mp_world_later(c->game, core, 0.1, "SUB_Remove");
}
static void escape_lava(re_ctx *c, qa_q1mp_actor *actor) {
  qa_q1b_game *g = c->game->base;
  if (!qa_q1mp_streq(g->host->contents(g->host->ctx, g->body(g, actor).origin), "lava")) return;
  qa_q1mp_actor *point = qa_q1mp_find0(c->game, "point1");
  if (point) g->set_origin(g, actor, g->body(g, point).origin);
}
static void goal(re_ctx *c, qa_q1mp_actor *actor, const char *target) {
  qa_q1b_game *g = c->game->base; (void)g;
  qa_q1mp_set_target(actor, target);
  qa_q1mp_actor *point = qa_q1mp_find0(c->game, target);
  if (!point) qa_range_error("End sequence %s placing screwed up!", target);
  qa_q1b_references_set(actor, "goalentity", QA_Q1MP_SOME(point->actor.id));
  qa_q1b_references_set(actor, "movetarget", QA_Q1MP_SOME(point->actor.id));
}
static void control(re_ctx *c, qa_q1b_game *g, qa_q1b_actor *actor) {
  qa_q1mp_actor *world = g->world;
  if (!world) qa_range_error("Ending requires worldspawn");
  double stage = qa_q1mp_number(world, "rogue:actorStage");
  if (stage == 0) { goal(c, actor, "point1"); actor->frame = 6; qa_q1mp_world_number(world, "rogue:actorStage", 1); qa_q1mp_world_later(c->game, actor, 0.1, "rogue:actor_run"); return; }
  if (stage == 2) { goal(c, actor, "machine"); qa_q1mp_world_number(world, "rogue:actorStage", 5); qa_q1mp_world_later(c->game, actor, 0.1, "rogue:actor_fire1"); return; }
  if (stage == 4) { actor->frame = 12; qa_q1mp_world_later(c->game, actor, 2, "rogue:actor_teleport"); return; }
  if (stage == 3) {
    qa_q1mp_set_target(actor, "timepod"); g->use_targets(g, actor, actor->activator);
    goal(c, actor, "point2"); actor->frame = 6; qa_q1mp_world_later(c->game, actor, 0.1, "rogue:actor_run");
  }
}
void qa_q1mp_start_rogue_ending(qa_q1mp_game *game, qa_actor_id player, const qa_q1mp_world_hooks *hooks) {
  re_ctx local = {game, hooks ? *hooks : (qa_q1mp_world_hooks){0}};
  re_ctx *c = ending_ctx ? ending_ctx : &local;
  qa_q1b_game *g = game->base; qa_q1mp_actor *world = g->world;
  if (!world || qa_q1mp_number(world, "rogue:cutscene_running") == 0 || qa_q1mp_number(world, "rogue:ending_started") != 0) return;
  qa_q1b_body body;
  if (!qa_q1mp_body_read(game, player, &body)) return;
  qa_q1mp_world_number(world, "rogue:ending_started", 1);
  qa_q1mp_actor *camera = qa_q1mp_find0(game, "cameraview");
  qa_q1mp_q1base *base = game->q1_base;
  if (base && base->begin_cutscene) base->begin_cutscene(base->ctx, g->map_name, QA_Q1MP_SOME(player), g->time + ((g->options.coop || !camera) ? 3 : 10000000));
  if (g->options.coop || !camera) {
    if (game->control_player) game->control_player(game->ctx, player, "cutscene", qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,48)), body.angles, QA_Q1MP_ZERO);
    if (game->emit_finale) game->emit_finale(game->ctx, qa_q1mp_mission_finale_text(g->options.edition, "$qc_finale_coop"), 4);
    remove_stuff(c); qa_q1mp_world_later(game, machine_of(game), 0.1, "rogue:time_crash"); return;
  }
  if (game->emit_finale) game->emit_finale(game->ctx, "", 1);
  qa_q1mp_actor *actor = g->create(g, "actor");
  actor->owner = QA_Q1MP_SOME(player); actor->max_health = 100; actor->solid = "slidebox"; actor->movement = "step";
  actor->frame = (hooks && hooks->player_frame) ? hooks->player_frame(hooks->ctx, player) : 0;
  qa_q1mp_set_model(actor, "progs/player.mdl");
  g->host->combat_set_health(g->host->ctx, actor->actor, 100);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = body.origin; p.has_angles = true; p.angles = body.angles;
  p.has_bounds = true; p.bounds = PLAYER_BOUNDS; qa_q1mp_patch(game, actor, &p);
  qa_q1mp_world_vector(actor, "view_ofs", QA_Q1MP_V3(0,0,25));
  actor->movement_flags |= 32; actor->ideal_yaw = body.angles.y; actor->yaw_speed = 20;
  qa_q1b_references_set(world, "rogue:theActor", QA_Q1MP_SOME(actor->actor.id));
  escape_lava(c, actor);
  qa_q1mp_world_later(game, actor, 0.1, "rogue:actor_control"); g->link(g, actor);
  qa_vec3 corigin = g->body(g, camera).origin;
  qa_vec3 cangles = qa_q1mp_velocity_angles(qa_q1mp_vsub(g->body(g, actor).origin, corigin));
  if (game->control_player) game->control_player(game->ctx, player, "cutscene", corigin, cangles, QA_Q1MP_ZERO);
  qa_q1mp_actor *tracker = g->create(g, "rogue_camera_tracker");
  tracker->owner = QA_Q1MP_SOME(player);
  qa_q1mp_world_later(game, tracker, 0.05, "rogue:track_camera");
}
static bool path_touch_impl(void *vctx, qa_q1mp_actor *corner, qa_q1mp_actor *mover) {
  re_ctx *c = vctx; qa_q1b_game *g = c->game->base;
  if (!qa_q1mp_streq(mover->classname,"actor") && !qa_q1mp_streq(mover->classname,"buzzsaw")) return false;
  qa_q1mp_opt_actor current = qa_q1b_references_get(mover, "movetarget");
  if (!current.has || !qa_same_actor(current.id, corner->actor.id)) return false;
  qa_q1mp_actor *next = qa_q1mp_find0(c->game, corner->target);
  qa_q1b_references_set(mover, "goalentity", next ? QA_Q1MP_SOME(next->actor.id) : QA_Q1MP_NONE);
  qa_q1b_references_set(mover, "movetarget", next ? QA_Q1MP_SOME(next->actor.id) : QA_Q1MP_NONE);
  mover->ideal_yaw = qa_q1b_yaw_for(qa_q1mp_vsub(next ? g->body(g, next).origin : QA_Q1MP_ZERO, g->body(g, mover).origin));
  if (!next) {
    qa_q1mp_world_number(mover, "pausetime", g->time + 999999);
    if (qa_q1mp_streq(mover->classname, "buzzsaw")) qa_q1mp_world_later(c->game, mover, 0.1, "rogue:saw_stand");
  }
  return true;
}
static void track_camera_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  re_ctx *c = vctx;
  qa_q1mp_opt_actor actor_ref = g->world ? qa_q1b_references_get(g->world, "rogue:theActor") : QA_Q1MP_NONE;
  qa_q1mp_actor *actor = actor_ref.has ? g->entity(g, actor_ref) : NULL;
  qa_q1b_body body; bool has = e->owner.has && qa_q1mp_body_read(c->game, e->owner.id, &body);
  if (!actor || !e->owner.has || !has) { g->remove(g, e); return; }
  qa_vec3 delta = qa_q1mp_vsub(g->body(g, actor).origin, body.origin);
  qa_vec3 neg = delta; neg.z = -delta.z;
  if (c->game->control_player) c->game->control_player(c->game->ctx, e->owner.id, "cutscene", body.origin, qa_q1mp_velocity_angles(neg), QA_Q1MP_ZERO);
  qa_q1mp_world_later(c->game, e, 0.1, "rogue:track_camera");
}
static void control_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { control(vctx, g, e); }
static void teleport_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  re_ctx *c = vctx; (void)qa_q1mp_ext_spawn_teleport_fog(c->game, g->body(g, e).origin);
  e->model[0] = '\0'; qa_q1mp_world_later(c->game, e, 999999, "SUB_Null");
}
static void run_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  re_ctx *c = vctx; escape_lava(c, e);
  qa_q1mp_opt_actor point_ref = qa_q1b_references_get(e, "goalentity");
  qa_q1mp_actor *point = point_ref.has ? g->entity(g, point_ref) : NULL;
  qa_q1mp_actor *world = g->world;
  if (point && (qa_q1mp_streq(point->targetname,"endpoint1")||qa_q1mp_streq(point->targetname,"endpoint2"))) {
    if (world) qa_q1mp_world_number(world, "rogue:actorStage", qa_q1mp_streq(point->targetname,"endpoint1") ? 2 : 4);
    qa_q1mp_world_later(c->game, e, 0.1, "rogue:actor_control"); return;
  }
  e->frame++; if (e->frame > 11) e->frame = 6;
  if (point && g->host->move_to_goal) g->host->move_to_goal(g->host->ctx, e->actor, point->actor.id, 15, NULL);
  qa_q1mp_world_later(c->game, e, 0.1, "rogue:actor_run");
}
typedef struct fire_ctx2 { re_ctx base; int stage; } fire_ctx2;
static fire_ctx2 fire_ctxs[21];
static void fire_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  fire_ctx2 *fc = vctx; re_ctx *c = &fc->base; int stage = fc->stage;
  e->frame = stage <= 6 ? 106 + stage : 12 + (stage - 7) % 5;
  if (stage == 1) {
    qa_q1mp_actor *target = machine_of(c->game);
    qa_q1b_references_set(e, "goalentity", QA_Q1MP_SOME(target->actor.id));
    target->pain = g->named.pain(g->named.ctx, target, "rogue:time_crash");
    target->die = g->named.die(g->named.ctx, target, "rogue:time_crash");
    g->host->combat_set_health(g->host->ctx, target->actor, 1);
    qa_vec3 angles = qa_q1mp_velocity_angles(qa_q1mp_vsub(g->body(g, target).origin, g->body(g, e).origin));
    qa_q1mp_set_angles_b(c->game, e, angles); e->effects = 2;
    qa_vec3 neg = angles; neg.x = -angles.x;
    qa_q1b_basis basis = qa_q1mp_vectors(c->game, neg);
    qa_q1mp_world_vector(e, "v_angle", neg);
    qa_q1mp_world_number(e, "ammo_rockets1", qa_q1mp_number(e,"ammo_rockets1") - 1);
    qa_q1mp_world_number(e, "currentammo", qa_q1mp_number(e,"ammo_rockets1"));
    qa_q1mp_sound_ch(c->game, e, "weapons/sgun1.wav", "weapon");
    qa_q1mp_actor *missile = qa_q1_create_missile_lifetime(g, QA_Q1MP_SOME(e->actor.id), "missile", "missile",
      qa_q1mp_vadd(qa_q1mp_vadd(g->body(g, e).origin, qa_q1mp_vscale(basis.forward, 8)), QA_Q1MP_V3(0,0,16)),
      qa_q1mp_vscale(qa_q1mp_aim(c->game, e->actor, basis.forward), 1000), 5);
    missile->projectile = "rocket";
    missile->touch = g->named.touch(g->named.ctx, missile, "projectile_touch");
    if (c->game->emit_finale) c->game->emit_finale(c->game->ctx, qa_q1mp_mission_finale_text(g->options.edition, "$qc_finale_rogue_end"), 4);
    qa_q1mp_q1base *base = c->game->q1_base;
    bool official = !base || !base->has_official_campaign || base->official_campaign;
    if (qa_q1mp_streq(g->options.edition,"rerelease") && official && qa_q1mp_streq(g->map_name,"r2m8") && c->game->emit_achievement) {
      c->game->emit_achievement(c->game->ctx, QA_Q1MP_NONE, "ACH_COMPLETE_R2M8");
      if (g->options.skill == 3) c->game->emit_achievement(c->game->ctx, QA_Q1MP_NONE, "ACH_COMPLETE_R2M8_NIGHTMARE");
    }
    qa_q1mp_start_finale_timer(c->game);
  } else if (stage == 2) {
    qa_vec3 a = g->body(g, e).angles; a.x = 0; qa_q1mp_set_angles_b(c->game, e, a);
    qa_vec3 v = qa_q1mp_vector_of(e, "v_angle"); v.x = 0; qa_q1mp_world_vector(e, "v_angle", v);
  } else if (stage == 5) {
    remove_stuff(c); qa_q1mp_actor *target = machine_of(c->game);
    if (g->health(g, target->actor.id) > 0) qa_q1mp_world_later(c->game, target, 0.1, "rogue:time_crash");
  } else if (stage == 6) e->effects = 0;
  else if (stage == 21 && g->world) qa_q1mp_world_number(g->world, "rogue:actorStage", 3);
  char name[48];
  if (stage == 21) qa_q1mp_world_later(c->game, e, 0.15, "rogue:actor_control");
  else { (void)snprintf(name, sizeof name, "rogue:actor_fire%d", stage + 1); qa_q1mp_world_later(c->game, e, stage == 1 ? 0.1 : 0.15, name); }
}
void qa_q1mp_register_rogue_ending(qa_q1mp_game *game) {
  re_ctx *c = malloc(sizeof *c); c->game = game; memset(&c->hooks, 0, sizeof c->hooks);
  ending_ctx = c; qa_q1b_game *g = game->base;
  if (game->register_path_touch) game->register_path_touch(game->ctx, "rogue:world_followers", c, path_touch_impl);
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = track_camera_cb;
  g->named.register_named(g->named.ctx, "rogue:track_camera", &h);
  qa_q1b_handlers hc = {0}; hc.ctx = c; hc.action = control_cb;
  g->named.register_named(g->named.ctx, "rogue:actor_control", &hc);
  qa_q1b_handlers ht = {0}; ht.ctx = c; ht.action = teleport_cb;
  g->named.register_named(g->named.ctx, "rogue:actor_teleport", &ht);
  qa_q1b_handlers hr = {0}; hr.ctx = c; hr.action = run_cb;
  g->named.register_named(g->named.ctx, "rogue:actor_run", &hr);
  for (int stage = 1; stage <= 21; stage++) {
    fire_ctxs[stage-1].base = *c; fire_ctxs[stage-1].stage = stage;
    char *name = malloc(48); (void)snprintf(name, 48, "rogue:actor_fire%d", stage);
    qa_q1b_handlers hf = {0}; hf.ctx = &fire_ctxs[stage-1]; hf.action = fire_cb;
    g->named.register_named(g->named.ctx, name, &hf);
  }
}
