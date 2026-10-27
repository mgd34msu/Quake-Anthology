/* Port of src/content/q1/missionpacks/rogue-weapons.ts. Observation
 * scans use game entities (see hipnotic_weapons.c note).
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/rogue_weapons.h"
#include "missionpacks_internal.h"
#include <math.h>
#include <stdlib.h>

typedef struct rw { qa_q1mp_game *game; } rw;
static rw *rw_new(qa_q1mp_game *g) { rw *h = malloc(sizeof *h); h->game = g; return h; }
static bool finish_w(qa_q1mp_game *game, qa_q1mp_player_state *player, double delay, const char *model, bool repeating) {
  delay = qa_q1mp_attack_delay(game, player, delay);
  player->attack_finished = qa_q1mp_fround(game->base->time + delay);
  player->next_weapon_frame = qa_q1mp_fround(game->base->time + (repeating ? qa_q1mp_frame_delay(game, player, 0.1) : delay));
  player->continuous_firing = repeating;
  player->weapon_frame = repeating ? (double)((int)player->weapon_frame % 8 + 1) : qa_q1mp_streq(player->weapon, "rogue:plasma") ? 0 : 1;
  player->weapon_animation_at = (repeating || qa_q1mp_streq(player->weapon, "rogue:plasma")) ? -1 : game->base->time;
  player->weapon_animation_base = 1; player->hostile_until = qa_q1mp_fround(game->base->time + 1);
  if (game->weapon_punch) game->weapon_punch(game->ctx, player, -2);
  if (game->emit_weapon) game->emit_weapon(game->ctx, player->actor.id, player->weapon, model, player->weapon_frame, -2);
  qa_q1b_body body;
  if (qa_q1mp_body_read(game, player->actor.id, &body)) qa_q1mp_effect_a(game, "muzzleflash", body.origin, QA_Q1MP_SOME(player->actor.id), 0);
  return true;
}
static qa_q1mp_actor *missile(qa_q1mp_game *game, const char *classname, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity, const char *model) {
  qa_q1b_game *g = game->base; qa_q1mp_actor *e = g->create(g, classname);
  e->owner = QA_Q1MP_SOME(owner); e->solid = "bbox"; e->movement = "flymissile"; qa_q1mp_set_model(e, model);
  g->set_bounds(g, e, QA_Q1MP_POINT);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = origin; p.has_velocity = true; p.velocity = velocity; p.has_angles = true; p.angles = qa_q1mp_velocity_angles(velocity); qa_q1mp_patch(game, e, &p);
  g->link(g, e); return e;
}
qa_q1mp_actor *qa_q1mp_launch_rogue_lava_spike(qa_q1mp_game *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction, bool powered) {
  qa_q1mp_actor *e = missile(game, "lava_spike", owner, origin, qa_q1mp_vscale(direction, 1000), "progs/lspike.mdl");
  e->count = powered ? 1 : 0; e->projectile_weapon = powered ? "rogue:lava-supernailgun" : "rogue:lava-nailgun";
  e->touch = game->base->named.touch(game->base->named.ctx, e, "rogue:lava-touch");
  game->base->schedule(game->base, e, 6, game->base->named.action(game->base->named.ctx, e, "SUB_Remove"));
  qa_q1mp_launch_behavior(game, e, owner, e->projectile_weapon, "nail"); return e;
}
bool qa_q1mp_fire_rogue_lava(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  double nails = qa_q1mp_count(game, player->actor.id, "rogue:ammo/lava-nails");
  bool powered = qa_q1mp_streq(player->weapon, "rogue:lava-supernailgun") && nails >= 2;
  if (!qa_q1mp_consume(game, player->actor, "rogue:ammo/lava-nails", powered ? 2 : 1)) return false;
  qa_q1b_body body;
  if (!qa_q1mp_body_read(game, player->actor.id, &body)) return false;
  qa_q1b_basis basis = qa_q1mp_vectors(game, player->view_angles);
  qa_vec3 origin = qa_q1mp_vadd(qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,16)), qa_q1mp_vscale(basis.right, powered ? 0 : player->nail_side * 4));
  game->base->sound_owned(game->base, player->actor, powered ? "weapons/spike2.wav" : "weapons/rocket1i.wav", QA_Q1MP_CH("weapon"), 1, 1);
  (void)qa_q1mp_launch_rogue_lava_spike(game, player->actor.id, origin, qa_q1mp_aim(game, player->actor, basis.forward), powered);
  player->nail_side *= -1;
  return finish_w(game, player, 0.2, qa_q1mp_streq(player->weapon,"rogue:lava-supernailgun") ? "progs/v_lava2.mdl" : "progs/v_lava.mdl", true);
}
static void lava_touch(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *entity, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; qa_q1mp_game *game = ((rw *)vctx)->game; qa_q1b_game *g = runtime;
  qa_q1mp_actor *other_e = g->entity(g, QA_Q1MP_SOME(other));
  if ((entity->owner.has && qa_same_actor(entity->owner.id, other)) || (other_e && qa_q1mp_streq(other_e->solid, "trigger"))) return;
  qa_vec3 origin = g->body(g, entity).origin;
  if (qa_q1mp_streq(g->host->contents(g->host->ctx, origin), "sky")) { g->remove(g, entity); return; }
  bool powered = entity->count == 1;
  if (qa_q1mp_can_damage_actor(game, other)) {
    qa_q1mp_effect_a(game, "blood", origin, QA_Q1MP_SOME(other), powered ? 18 : 9);
    if (!qa_q1mp_streq(g->host->classname(g->host->ctx, other), "monster_lava_man")) {
      bool is_player = g->is_player(g, QA_Q1MP_SOME(other));
      double damage = is_player ? (powered ? 18 : 9) : (powered ? 30 : 15);
      qa_q1mp_damage_full(game, other, entity->actor.id, entity->owner, damage, entity->projectile_weapon, "direct", powered ? "rogue:super-lava" : "rogue:lava", is_player ? (powered ? "half-effectiveness" : "bypass") : NULL);
    }
  } else qa_q1mp_effect_a(game, powered ? "superspike" : "spike", origin, QA_Q1MP_NONE, 0);
  g->remove(g, entity);
}
qa_q1mp_actor *qa_q1mp_launch_rogue_multi_grenade(qa_q1mp_game *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles) {
  qa_q1mp_actor *grenade = missile(game, "MultiGrenade", owner, origin, velocity, "progs/mervup.mdl");
  grenade->movement = "bounce"; grenade->mangle = angles; grenade->angular_velocity = QA_Q1MP_V3(300,300,300); grenade->projectile_weapon = "rogue:multi-grenade";
  grenade->touch = game->base->named.touch(game->base->named.ctx, grenade, "rogue:multi-grenade-touch");
  game->base->schedule(game->base, grenade, 1, game->base->named.action(game->base->named.ctx, grenade, "rogue:multi-grenade-split"));
  qa_q1mp_launch_behavior(game, grenade, owner, "rogue:multi-grenade", "grenade"); return grenade;
}
bool qa_q1mp_fire_rogue_multi_grenade(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  if (!qa_q1mp_consume(game, player->actor, "rogue:ammo/multi-rockets", 1)) return false;
  qa_q1b_body body;
  if (!qa_q1mp_body_read(game, player->actor.id, &body)) return false;
  qa_vec3 velocity = qa_q1mp_grenade_velocity(game, player->view_angles, qa_q1mp_aim(game, player->actor, qa_q1mp_vectors(game, player->view_angles).forward));
  (void)qa_q1mp_launch_rogue_multi_grenade(game, player->actor.id, body.origin, velocity, QA_Q1MP_ZERO);
  game->base->sound_owned(game->base, player->actor, "weapons/grenade.wav", QA_Q1MP_CH("weapon"), 1, 1);
  return finish_w(game, player, 0.6, "progs/v_multi.mdl", false);
}
static void grenade_explode_mini(qa_q1mp_game *game, qa_q1mp_actor *grenade, bool mini) {
  bool player_owned = grenade->owner.has && game->base->is_player(game->base, grenade->owner);
  game->base->radius_damage(game->base, grenade->actor.id, grenade->owner, mini ? (player_owned ? 90 : 60) : 120, QA_Q1MP_NONE, "rogue:multi-grenade", NULL);
  qa_q1mp_effect_a(game, "explosion", game->base->body(game->base, grenade).origin, QA_Q1MP_NONE, 0);
  game->base->remove(game->base, grenade);
}
static void split_grenade(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *grenade) {
  qa_q1mp_game *game = ((rw *)vctx)->game; qa_q1b_game *g = runtime;
  if (!grenade->owner.has) { g->remove(g, grenade); return; }
  static const double offsets[] = {0, 72, 144, 216, 288};
  for (int i = 0; i < 5; i++) {
    qa_vec3 angles = grenade->mangle; angles.y = grenade->mangle.y + offsets[i];
    qa_q1b_basis basis = qa_q1mp_vectors(game, angles);
    qa_vec3 velocity = qa_q1mp_vadd(qa_q1mp_vscale(basis.forward, 100), qa_q1mp_vscale(basis.up, 400));
    velocity = qa_q1mp_vadd(velocity, qa_q1mp_vscale(basis.forward, (qa_q1mp_random(game) * 2 - 1) * 60 - 30));
    velocity = qa_q1mp_vadd(velocity, qa_q1mp_vscale(basis.right, (qa_q1mp_random(game) * 2 - 1) * 40 - 20));
    velocity = qa_q1mp_vadd(velocity, qa_q1mp_vscale(basis.up, (qa_q1mp_random(game) * 2 - 1) * 60 - 30));
    qa_q1mp_actor *mini = missile(game, "MiniGrenade", grenade->owner.id, g->body(g, grenade).origin, velocity, "progs/mervup.mdl");
    mini->movement = "bounce"; mini->mangle = angles; mini->angular_velocity = QA_Q1MP_V3(300,300,300); mini->projectile_weapon = "rogue:multi-grenade";
    mini->touch = g->named.touch(g->named.ctx, mini, "rogue:multi-grenade-touch");
    qa_q1mp_launch_behavior(game, mini, grenade->owner.id, "rogue:multi-grenade", "grenade");
    g->schedule(g, mini, 1 + (qa_q1mp_random(game) * 2 - 1) * 0.5, g->named.action(g->named.ctx, mini, "rogue:mini-grenade-explode"));
  }
  g->remove(g, grenade);
}
static void multi_grenade_touch(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *grenade, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; qa_q1mp_game *game = ((rw *)vctx)->game; qa_q1b_game *g = runtime;
  if (grenade->owner.has && qa_same_actor(grenade->owner.id, other)) return;
  qa_q1mp_actor *other_e = g->entity(g, QA_Q1MP_SOME(other));
  if ((other_e && other_e->aimed_damage) || g->is_player(g, QA_Q1MP_SOME(other))) {
    grenade_explode_mini(game, grenade, qa_q1mp_streq(grenade->classname,"MiniGrenade") || !grenade->owner.has || !g->is_player(g, grenade->owner)); return;
  }
  qa_q1mp_sound_ch(game, grenade, "weapons/bounce.wav", "weapon");
  if (qa_q1mp_length(g->body(g, grenade).velocity) == 0) grenade->angular_velocity = QA_Q1MP_ZERO;
}
static void explode_rocket(qa_q1mp_game *game, qa_q1mp_actor *rocket, qa_q1mp_opt_actor direct) {
  qa_q1b_game *g = game->base;
  if (direct.has && g->health(g, direct.id) != 0) {
    double damage = 60 + qa_q1mp_random(game) * 15;
    const char *cn = g->host->classname(g->host->ctx, direct.id);
    if (qa_q1mp_streq(cn,"monster_shambler")||qa_q1mp_streq(cn,"monster_dragon")) damage *= 0.5;
    qa_q1mp_damage_w(game, direct.id, rocket->actor.id, rocket->owner, damage, "rogue:multi-rocket");
  }
  g->radius_damage(g, rocket->actor.id, rocket->owner, 75, direct, "rogue:multi-rocket", NULL);
  qa_q1b_body b = g->body(g, rocket);
  qa_q1mp_effect_a(game, "explosion", qa_q1mp_vsub(b.origin, qa_q1mp_vscale(qa_q1mp_normalize(b.velocity), 8)), QA_Q1MP_NONE, 0);
  g->remove(g, rocket);
}
static void home_rocket(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *rocket);
static void acquire_rocket(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *rocket) {
  qa_q1mp_game *game = ((rw *)vctx)->game; qa_q1b_game *g = runtime;
  if (rocket->delay < g->time) { explode_rocket(game, rocket, QA_Q1MP_NONE); return; }
  qa_q1b_body body = g->body(g, rocket);
  qa_owned_actor owner; bool has_owner = rocket->owner.has && g->host->actors_resolve_owned(g->host->ctx, rocket->owner.id, &owner);
  qa_vec3 direction = !has_owner ? qa_q1mp_normalize(body.velocity) : qa_q1mp_aim(game, rocket->actor, qa_q1mp_vectors(game, rocket->mangle).forward);
  qa_q1b_trace trace = qa_q1mp_trace(game, body.origin, qa_q1mp_vadd(body.origin, qa_q1mp_vscale(direction, 1000)), QA_Q1MP_SOME(rocket->actor.id), true, false);
  if (trace.actor.has) { qa_q1mp_actor *te = g->entity(g, trace.actor); if (te && te->monster) { qa_q1mp_set_mission_reference(rocket, "rogue:enemy", trace.actor); home_rocket(vctx, runtime, rocket); return; } }
  rocket->mangle = qa_q1mp_velocity_angles(body.velocity);
  g->schedule(g, rocket, 0.2, g->named.action(g->named.ctx, rocket, "rogue:rocket-acquire"));
}
static void home_rocket(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *rocket) {
  qa_q1mp_game *game = ((rw *)vctx)->game; qa_q1b_game *g = runtime;
  qa_q1mp_opt_actor target = qa_q1mp_mission_reference(game, rocket, "rogue:enemy");
  qa_q1b_body body; bool has = target.has && qa_q1mp_body_read(game, target.id, &body);
  if (!has || !target.has || g->health(g, target.id) < 1) { g->remove(g, rocket); return; }
  if (!(game->controls_trajectory && game->controls_trajectory(game->ctx, rocket->actor.id)))
    qa_q1mp_move_missile(game, rocket, qa_q1mp_vscale(qa_q1mp_normalize(qa_q1mp_vsub(body.origin, g->body(g, rocket).origin)), 1000));
  g->schedule(g, rocket, 0.1, g->named.action(g->named.ctx, rocket, "rogue:rocket-home"));
}
bool qa_q1mp_fire_rogue_multi_rocket(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  if (!qa_q1mp_consume(game, player->actor, "rogue:ammo/multi-rockets", 1)) return false;
  qa_q1b_body body;
  if (!qa_q1mp_body_read(game, player->actor.id, &body)) return false;
  (void)qa_q1mp_vectors(game, player->view_angles);
  bool multiplayer = game->base->options.coop || game->base->options.deathmatch != 0;
  static const struct { double offset; double frame; } shots[] = {{-10,2},{-5,3},{5,0},{10,1}};
  for (int i = 0; i < 4; i++) {
    qa_vec3 origin = qa_q1mp_vadd(qa_q1mp_vadd(body.origin, qa_q1mp_vscale(game->base->basis.forward, 8)), QA_Q1MP_V3(0,0,16));
    qa_vec3 angles = player->view_angles; angles.y = player->view_angles.y + shots[i].offset * 0.66;
    qa_q1b_basis basis = qa_q1mp_vectors(game, multiplayer ? angles : player->view_angles);
    qa_vec3 velocity = multiplayer ? qa_q1mp_vscale(qa_q1mp_aim(game, player->actor, basis.forward), 1000)
                       : qa_q1mp_vsub(qa_q1mp_vscale(basis.forward, 1000), qa_q1mp_vscale(basis.right, shots[i].offset * 8));
    qa_q1mp_actor *rocket = missile(game, "MultiRocket", player->actor.id, origin, velocity, multiplayer ? "progs/rockup_d.mdl" : "progs/rockup.mdl");
    rocket->projectile_weapon = "rogue:multi-rocket"; rocket->frame = shots[i].frame;
    rocket->delay = qa_q1mp_fround(game->base->time + 4); rocket->mangle = player->view_angles;
    rocket->touch = game->base->named.touch(game->base->named.ctx, rocket, "rogue:rocket-touch");
    qa_q1mp_launch_behavior(game, rocket, player->actor.id, "rogue:multi-rocket", "rocket");
    if (multiplayer) game->base->schedule(game->base, rocket, 4, game->base->named.action(game->base->named.ctx, rocket, "rogue:rocket-explode"));
    else {
      qa_q1b_trace trace = qa_q1mp_trace(game, origin, qa_q1mp_vadd(origin, velocity), QA_Q1MP_SOME(player->actor.id), true, false);
      qa_q1mp_actor *te = trace.actor.has ? game->base->entity(game->base, trace.actor) : NULL;
      if (trace.actor.has && te && te->monster) {
        qa_q1mp_set_mission_reference(rocket, "rogue:enemy", trace.actor);
        rocket->think = game->base->named.action(game->base->named.ctx, rocket, "rogue:rocket-home");
      } else game->base->schedule(game->base, rocket, 0.1, game->base->named.action(game->base->named.ctx, rocket, "rogue:rocket-acquire"));
    }
  }
  game->base->sound_owned(game->base, player->actor, "weapons/sgun1.wav", QA_Q1MP_CH("weapon"), 1, 1);
  return finish_w(game, player, 0.8, "progs/v_multi2.mdl", false);
}
qa_q1mp_actor *qa_q1mp_launch_rogue_plasma(qa_q1mp_game *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction) {
  qa_q1mp_actor *plasma = missile(game, "plasma", owner, origin, qa_q1mp_vscale(direction, 0.01), "progs/plasma.mdl");
  plasma->angular_velocity = QA_Q1MP_V3(300,300,300); plasma->projectile_weapon = "rogue:plasma";
  if (!game->base->options.coop && game->base->options.deathmatch == 0) plasma->effects = 4;
  plasma->touch = game->base->named.touch(game->base->named.ctx, plasma, "rogue:plasma-touch");
  qa_q1mp_sound_ch(game, plasma, "plasma/flight.wav", "weapon");
  game->base->schedule(game->base, plasma, 0.1, game->base->named.action(game->base->named.ctx, plasma, "rogue:plasma-launch"));
  qa_q1mp_launch_behavior(game, plasma, owner, "rogue:plasma", "plasma"); return plasma;
}
bool qa_q1mp_fire_rogue_plasma(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  double ammo = qa_q1mp_count(game, player->actor.id, "rogue:ammo/plasma");
  if (ammo < 1) return false;
  if (player->water_level > 1) {
    (void)qa_q1mp_consume(game, player->actor, "rogue:ammo/plasma", ammo);
    game->base->radius_damage(game->base, player->actor.id, QA_Q1MP_SOME(player->actor.id), 35 * ammo, QA_Q1MP_NONE, "rogue:plasma", NULL);
  } else {
    qa_q1b_body body;
    if (!qa_q1mp_body_read(game, player->actor.id, &body)) return false;
    (void)qa_q1mp_consume(game, player->actor, "rogue:ammo/plasma", 1);
    qa_q1b_basis basis = qa_q1mp_vectors(game, player->view_angles);
    qa_vec3 origin = qa_q1mp_vadd(qa_q1mp_vadd(body.origin, qa_q1mp_vscale(basis.forward, 24)), QA_Q1MP_V3(0,0,16));
    (void)qa_q1mp_launch_rogue_plasma(game, player->actor.id, origin, qa_q1mp_aim(game, player->actor, basis.forward));
    if (game->emit_sound_event) game->emit_sound_event(game->ctx, player->actor.id, "plasma/fire.wav", QA_Q1MP_CH("weapon"), 0.5, 1);
  }
  return finish_w(game, player, 1, "progs/v_plasma.mdl", false);
}
static void plasma_damage(qa_q1mp_game *game, qa_q1mp_actor *plasma, qa_vec3 end) {
  qa_vec3 origin = game->base->body(game->base, plasma).origin, delta = qa_q1mp_vsub(end, origin);
  qa_vec3 side = {-delta.y * 16, -delta.y * 16, 0};
  qa_vec3 offsets[3] = {QA_Q1MP_ZERO, side, qa_q1mp_vscale(side, -1)}; qa_actor_id hit[8]; size_t hits = 0;
  for (int i = 0; i < 3; i++) {
    qa_q1b_trace trace = qa_q1mp_trace(game, qa_q1mp_vadd(origin, offsets[i]), qa_q1mp_vadd(end, offsets[i]), QA_Q1MP_SOME(plasma->actor.id), true, false);
    if (!trace.actor.has) continue;
    bool seen = false;
    for (size_t j = 0; j < hits; j++) if (qa_same_actor(hit[j], trace.actor.id)) seen = true;
    if (seen) continue;
    if (hits < 8) hit[hits++] = trace.actor.id;
    if (qa_q1mp_can_damage_actor(game, trace.actor.id)) {
      if (game->emit_particles) game->emit_particles(game->ctx, trace.end, QA_Q1MP_V3(0,0,100), 225, 200);
      qa_q1mp_damage_w(game, trace.actor.id, plasma->actor.id, plasma->owner, 50, "rogue:plasma");
    }
  }
}
static void plasma_touch(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *plasma, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; qa_q1mp_game *game = ((rw *)vctx)->game; qa_q1b_game *g = runtime;
  if (plasma->owner.has && qa_same_actor(plasma->owner.id, other)) return;
  qa_vec3 origin = g->body(g, plasma).origin;
  if (qa_q1mp_streq(g->host->contents(g->host->ctx, origin), "sky")) { g->remove(g, plasma); return; }
  double damage = 80 + qa_q1mp_random(game) * 20;
  qa_q1mp_sound_ch(game, plasma, "plasma/explode.wav", "weapon");
  if (g->health(g, other) != 0) {
    if (qa_q1mp_streq(g->host->classname(g->host->ctx, other), "monster_shambler")) damage *= 0.5;
    qa_q1mp_damage_w(game, other, plasma->actor.id, plasma->owner, damage, "rogue:plasma");
  }
  g->radius_damage(g, plasma->actor.id, plasma->owner, 70, QA_Q1MP_SOME(other), "rogue:plasma", NULL);
  qa_q1mp_effect_a(game, "explosion", origin, QA_Q1MP_NONE, 0);
  int count = 0; qa_q1mp_actor *ents[512]; size_t total = g->entities(g, ents, 512);
  for (size_t idx = total; idx-- > 0;) {
    qa_actor_id target = ents[idx]->actor.id; qa_q1b_body body;
    if (!qa_q1mp_body_read(game, target, &body)) continue;
    if (plasma->owner.has && qa_same_actor(plasma->owner.id, target)) continue;
    if (!(g->is_player(g, QA_Q1MP_SOME(target)) || ents[idx]->monster)) continue;
    qa_vec3 center = qa_q1mp_vadd(body.origin, qa_q1mp_vscale(qa_q1mp_vadd(body.bounds.min, body.bounds.max), 0.5));
    if (qa_q1mp_length(qa_q1mp_vsub(center, origin)) > 320) continue;
    qa_q1b_trace trace = qa_q1mp_trace(game, origin, body.origin, QA_Q1MP_NONE, false, false);
    if (trace.fraction != 1) continue;
    if (game->emit_beam) game->emit_beam(game->ctx, "lightning2", target, body.origin, origin);
    qa_q1mp_sound_ch(game, plasma, "weapons/lhit.wav", "voice"); plasma_damage(game, plasma, body.origin);
    if (++count == 5) break;
  }
  g->remove(g, plasma);
}
static void mini_explode_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e) { (void)r; grenade_explode_mini(((rw *)vctx)->game, e, true); }
static void rocket_explode_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e) { (void)r; explode_rocket(((rw *)vctx)->game, e, QA_Q1MP_NONE); }
static void rocket_touch_cb(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *rocket, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s;
  if (rocket->owner.has && qa_same_actor(rocket->owner.id, other)) return;
  if (qa_q1mp_streq(runtime->host->contents(runtime->host->ctx, runtime->body(runtime, rocket).origin), "sky")) { runtime->remove(runtime, rocket); return; }
  explode_rocket(((rw *)vctx)->game, rocket, QA_Q1MP_SOME(other));
}
static void plasma_launch_cb(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *plasma) {
  qa_q1mp_game *game = ((rw *)vctx)->game;
  if (!(game->controls_trajectory && game->controls_trajectory(game->ctx, plasma->actor.id)))
    qa_q1mp_move_missile(game, plasma, qa_q1mp_vscale(qa_q1mp_normalize(runtime->body(runtime, plasma).velocity), 1250));
  runtime->schedule(runtime, plasma, 5, runtime->named.action(runtime->named.ctx, plasma, "SUB_Remove"));
}
void qa_q1mp_register_rogue_weapon_callbacks(qa_q1mp_game *game) {
  rw *c = rw_new(game);
  qa_q1b_handlers h = {0};
#define REG_TOUCH(name, fn) do { memset(&h, 0, sizeof h); h.ctx = c; h.touch = (fn); game->base->named.register_named(game->base->named.ctx, (name), &h); } while (0)
#define REG_ACT(name, fn) do { memset(&h, 0, sizeof h); h.ctx = c; h.action = (fn); game->base->named.register_named(game->base->named.ctx, (name), &h); } while (0)
  REG_TOUCH("rogue:lava-touch", lava_touch);
  REG_TOUCH("rogue:multi-grenade-touch", multi_grenade_touch);
  REG_ACT("rogue:multi-grenade-split", split_grenade);
  REG_ACT("rogue:mini-grenade-explode", mini_explode_cb);
  REG_ACT("rogue:rocket-explode", rocket_explode_cb);
  REG_ACT("rogue:rocket-acquire", acquire_rocket);
  REG_ACT("rogue:rocket-home", home_rocket);
  REG_TOUCH("rogue:rocket-touch", rocket_touch_cb);
  REG_TOUCH("rogue:plasma-touch", plasma_touch);
  REG_ACT("rogue:plasma-launch", plasma_launch_cb);
#undef REG_TOUCH
#undef REG_ACT
}
