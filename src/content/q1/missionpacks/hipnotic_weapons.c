/* Port of src/content/q1/missionpacks/hipnotic-weapons.ts.
 * Observation scans use game entities (q1b host has no observations()
 * list; TS iterates host.actors.observations() reversed - order note
 * in unresolved). GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/hipnotic_weapons.h"
#include "missionpacks_internal.h"
#include <stdlib.h>

const qa_q1mp_hipnotic_laser_profile qa_q1mp_hipnotic_laser_default = {"hipnotic:laser", 18, 25};
typedef struct hw { qa_q1mp_game *game; } hw;
static hw *hw_new(qa_q1mp_game *g) { hw *h = malloc(sizeof *h); h->game = g; return h; }
static bool finish_w(qa_q1mp_game *game, qa_q1mp_player_state *player, double delay, double frame, const char *model, double punch) {
  player->attack_finished = qa_q1mp_fround(game->base->time + qa_q1mp_attack_delay(game, player, delay));
  player->next_weapon_frame = player->attack_finished; player->weapon_frame = frame;
  player->hostile_until = qa_q1mp_fround(game->base->time + 1);
  if (game->weapon_punch) game->weapon_punch(game->ctx, player, punch);
  if (game->emit_weapon) game->emit_weapon(game->ctx, player->actor.id, player->weapon, model, frame, punch);
  qa_q1b_body body; if (qa_q1mp_body_read(game, player->actor.id, &body)) qa_q1mp_effect_a(game, "muzzleflash", body.origin, QA_Q1MP_SOME(player->actor.id), 0);
  return true;
}
qa_q1mp_actor *qa_q1mp_launch_hipnotic_laser(qa_q1mp_game *game, qa_actor_id shooter, qa_vec3 origin, qa_vec3 direction, bool light, const qa_q1mp_hipnotic_laser_profile *profile) {
  if (!profile) profile = &qa_q1mp_hipnotic_laser_default;
  qa_q1b_game *g = game->base; qa_q1mp_actor *laser = g->create(g, "hiplaser");
  qa_vec3 velocity = qa_q1mp_vscale(qa_q1mp_normalize(direction), 1000);
  laser->owner = QA_Q1MP_SOME(shooter); laser->activator = QA_Q1MP_SOME(shooter);
  laser->movement = "flymissile"; laser->solid = "bbox"; qa_q1mp_set_model(laser, "progs/lasrspik.mdl");
  laser->effects = light ? 8 : 0; laser->speed = 1000; laser->damage = light ? profile->light_damage : profile->damage;
  laser->angular_velocity = QA_Q1MP_V3(0,0,400); laser->movedir = velocity;
  laser->attack_finished = qa_q1mp_fround(g->time + 5); laser->projectile_weapon = profile->weapon;
  laser->touch = g->named.touch(g->named.ctx, laser, "hipnotic:laser-touch");
  g->set_bounds(g, laser, QA_Q1MP_POINT);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = origin; p.has_velocity = true; p.velocity = velocity; p.has_angles = true; p.angles = qa_q1mp_velocity_angles(velocity); qa_q1mp_patch(game, laser, &p);
  g->link(g, laser); g->schedule(g, laser, 0, g->named.action(g->named.ctx, laser, "hipnotic:laser-think"));
  qa_owned_actor owner; if (g->host->actors_resolve_owned(g->host->ctx, shooter, &owner)) g->sound_owned(g, owner, "hipweap/laserg.wav", QA_Q1MP_CH("weapon"), 1, 1);
  qa_q1mp_launch_behavior(game, laser, shooter, profile->weapon, "bolt");
  return laser;
}
bool qa_q1mp_fire_hipnotic_laser(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  if (!qa_q1mp_consume(game, player->actor, "q1:ammo/cells", 1)) return false;
  qa_q1b_body body; if (!qa_q1mp_body_read(game, player->actor.id, &body)) return false;
  qa_q1b_basis basis = qa_q1mp_vectors(game, player->view_angles);
  qa_vec3 flat = basis.forward; flat.z = 0; qa_vec3 outward = qa_q1mp_normalize(flat);
  qa_vec3 origin = qa_q1mp_vadd(qa_q1mp_vadd(body.origin, qa_q1mp_vscale(basis.up, 6)), qa_q1mp_vscale(outward, 12));
  qa_vec3 direction = qa_q1mp_aim(game, player->actor, basis.forward);
  bool paired = !player->continuous_firing || player->weapon_frame == 4;
  if (paired) {
    double offset = 6 * 0.707;
    origin = qa_q1mp_vsub(qa_q1mp_vadd(origin, qa_q1mp_vscale(basis.right, offset)), qa_q1mp_vscale(basis.up, offset));
    (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, origin, direction, false, NULL);
    (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, qa_q1mp_vsub(origin, qa_q1mp_vscale(basis.right, offset * 2)), direction, false, NULL);
  } else (void)qa_q1mp_launch_hipnotic_laser(game, player->actor.id, qa_q1mp_vadd(origin, qa_q1mp_vscale(basis.up, 6)), direction, qa_q1mp_random(game) < 0.1, NULL);
  player->continuous_firing = true; player->weapon_animation_at = -1;
  return finish_w(game, player, 0.1, paired ? 1 : 4, "progs/v_laserg.mdl", -1);
}
static void laser_touch(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *laser, qa_actor_id other, bool has_normal, qa_vec3 normal, const void *surf) {
  (void)surf; qa_q1mp_game *game = ((hw *)vctx)->game; qa_q1b_game *g = runtime;
  laser->owner = QA_Q1MP_NONE; laser->count++;
  qa_vec3 origin = g->body(g, laser).origin;
  if (qa_q1mp_streq(g->host->contents(g->host->ctx, origin), "sky")) { g->remove(g, laser); return; }
  qa_vec3 old_dir = qa_q1mp_normalize(laser->movedir);
  qa_q1b_trace trace = qa_q1mp_trace(game, qa_q1mp_vsub(origin, qa_q1mp_vscale(old_dir,16)), qa_q1mp_vadd(origin, qa_q1mp_vscale(old_dir,16)), QA_Q1MP_SOME(laser->actor.id), true, false);
  g->set_origin(g, laser, trace.end);
  if (g->health(g, other) != 0) {
    if (laser->activator.has && qa_same_actor(laser->activator.id, other)) laser->damage = qa_q1mp_fround(laser->damage / 2);
    qa_q1mp_effect_a(game, "blood", trace.end, QA_Q1MP_SOME(other), laser->damage);
    qa_q1mp_damage_w(game, other, laser->actor.id, laser->activator, laser->damage, laser->projectile_weapon);
  } else if (laser->count == 3 || qa_q1mp_random(game) < 0.15) qa_q1mp_effect_a(game, "gunshot", trace.end, QA_Q1MP_NONE, 0);
  else {
    laser->damage = qa_q1mp_fround(laser->damage * 0.9);
    qa_vec3 n = trace.fraction < 1 ? trace.normal : (has_normal ? normal : QA_Q1MP_ZERO);
    laser->movedir = qa_q1mp_vscale(qa_q1mp_normalize(qa_q1mp_vadd(old_dir, qa_q1mp_vscale(n, 2))), laser->speed);
    qa_q1mp_move_missile(game, laser, laser->movedir); (void)qa_q1mp_random(game);
    g->sound(g, laser, "hipweap/laserric.wav", QA_Q1MP_CH("weapon"), 3, 1); return;
  }
  g->sound(g, laser, "enforcer/enfstop.wav", QA_Q1MP_CH("weapon"), 3, 1); g->remove(g, laser);
}
qa_q1mp_actor *qa_q1mp_launch_hipnotic_proximity(qa_q1mp_game *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 velocity) {
  qa_q1b_game *g = game->base; qa_q1mp_actor *mine = g->create(g, "proximity_grenade");
  mine->owner = QA_Q1MP_SOME(owner); mine->activator = QA_Q1MP_SOME(owner); mine->movement = "toss"; mine->solid = "bbox";
  qa_q1mp_set_model(mine, "progs/proxbomb.mdl"); mine->angular_velocity = QA_Q1MP_V3(100,600,100); mine->projectile_weapon = "hipnotic:proximity";
  mine->delay = qa_q1mp_fround(g->time + 15 + 10 * qa_q1mp_random(game));
  qa_q1b_actor_set_damageable(g, mine, false); g->host->combat_set_health(g->host->ctx, mine->actor, 5);
  mine->touch = g->named.touch(g->named.ctx, mine, "hipnotic:proximity-touch");
  mine->die = g->named.die(g->named.ctx, mine, "hipnotic:proximity-arm-explosion");
  g->set_bounds(g, mine, (qa_bounds){{-1,-1,-1},{1,1,1}});
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = origin; p.has_velocity = true; p.velocity = velocity; p.has_angles = true; p.angles = qa_q1mp_velocity_angles(velocity); qa_q1mp_patch(game, mine, &p);
  g->link(g, mine); g->schedule(g, mine, 2, g->named.action(g->named.ctx, mine, "hipnotic:proximity-watch"));
  qa_q1mp_launch_behavior(game, mine, owner, "hipnotic:proximity", "grenade");
  return mine;
}
bool qa_q1mp_fire_hipnotic_proximity(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  if (!qa_q1mp_consume(game, player->actor, "q1:ammo/rockets", 1)) return false;
  qa_q1b_body body; if (!qa_q1mp_body_read(game, player->actor.id, &body)) return false;
  qa_vec3 velocity = qa_q1mp_grenade_velocity(game, player->view_angles, qa_q1mp_aim(game, player->actor, qa_q1mp_vectors(game, player->view_angles).forward));
  (void)qa_q1mp_launch_hipnotic_proximity(game, player->actor.id, body.origin, velocity);
  game->base->sound_owned(game->base, player->actor, "hipweap/proxbomb.wav", QA_Q1MP_CH("weapon"), 1, 1);
  player->continuous_firing = false; player->weapon_animation_at = game->base->time; player->weapon_animation_base = 1;
  return finish_w(game, player, 0.6, 1, "progs/v_prox.mdl", -2);
}
static void proximity_explode_impl(qa_q1mp_game *game, qa_q1mp_actor *mine) {
  game->base->radius_damage(game->base, mine->actor.id, mine->activator, 95, QA_Q1MP_NONE, "hipnotic:proximity", NULL);
  qa_q1mp_effect_a(game, "explosion", game->base->body(game->base, mine).origin, QA_Q1MP_NONE, 0);
  game->base->remove(game->base, mine);
}
static void arm_explosion(qa_q1mp_game *game, qa_q1mp_actor *mine, double delay) {
  qa_q1b_actor_set_damageable(game->base, mine, false);
  qa_q1mp_set_mission_number(mine, "hipnotic:detonating", 1); mine->owner = mine->activator;
  game->base->schedule(game->base, mine, delay, game->base->named.action(game->base->named.ctx, mine, "hipnotic:proximity-explode"));
}
static void proximity_touch(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *mine, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn;(void)n;(void)s; qa_q1mp_game *game = ((hw *)vctx)->game; qa_q1b_game *g = runtime;
  if (qa_same_actor(other, mine->actor.id) || qa_q1mp_streq(g->host->classname(g->host->ctx, other), mine->classname)) return;
  mine->movement = "toss";
  if (mine->count == 1) return;
  qa_q1b_body body; bool has = qa_q1mp_body_read(game, other, &body);
  qa_q1mp_actor *other_e = g->entity(g, QA_Q1MP_SOME(other));
  if ((has && qa_q1mp_length(body.velocity) > 0) || (other_e && other_e->aimed_damage) || g->is_player(g, QA_Q1MP_SOME(other))) {
    arm_explosion(game, mine, 0.1); proximity_explode_impl(game, mine); return;
  }
  qa_q1mp_sound_ch(game, mine, "weapons/bounce.wav", "weapon"); mine->movement = "none"; mine->count = 1;
  qa_q1mp_set_mission_reference(mine, "hipnotic:surface", QA_Q1MP_SOME(other));
  g->set_bounds(g, mine, (qa_bounds){{-8,-8,-8},{8,8,8}}); g->link(g, mine);
}
static void proximity_watch(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *mine) {
  qa_q1mp_game *game = ((hw *)vctx)->game; qa_q1b_game *g = runtime;
  qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512); size_t mines = 0;
  for (size_t i = 0; i < n; i++) if (qa_q1mp_streq(ents[i]->classname, mine->classname) && qa_q1mp_number(ents[i], "hipnotic:detonating") == 0) mines++;
  qa_q1mp_opt_actor surface = qa_q1mp_mission_reference(game, mine, "hipnotic:surface");
  qa_q1b_body sb; bool has_sb = surface.has && qa_q1mp_body_read(game, surface.id, &sb);
  if (g->time > mine->delay || mines > 15 || (has_sb && qa_q1mp_length(sb.velocity) > 0)) { proximity_explode_impl(game, mine); return; }
  mine->owner = QA_Q1MP_NONE; qa_q1b_actor_set_damageable(g, mine, true);
  qa_vec3 origin = g->body(g, mine).origin;
  for (size_t idx = n; idx-- > 0;) {
    qa_actor_id other = ents[idx]->actor.id; qa_q1b_body body;
    if (!qa_q1mp_body_read(game, other, &body)) continue;
    qa_vec3 center = qa_q1mp_vadd(body.origin, qa_q1mp_vscale(qa_q1mp_vadd(body.bounds.min, body.bounds.max), 0.5));
    if (qa_q1mp_length(qa_q1mp_vsub(origin, center)) > 140) continue;
    qa_q1mp_actor *target = ents[idx];
    bool live_enemy = !qa_same_actor(other, mine->actor.id) && g->health(g, other) > 0 && (g->is_player(g, QA_Q1MP_SOME(other)) || target->monster != NULL) && !qa_q1mp_streq(target->classname, mine->classname);
    bool loose = qa_q1mp_streq(target->classname, mine->classname) && target->count == 0;
    if (!live_enemy && !loose) continue;
    qa_q1b_trace trace = qa_q1mp_trace(game, origin, body.origin, QA_Q1MP_SOME(mine->actor.id), false, false);
    if (trace.fraction != 1) continue;
    qa_q1mp_sound_ch(game, mine, "hipweap/proxwarn.wav", "weapon"); arm_explosion(game, mine, 0.5); return;
  }
  g->schedule(g, mine, 0.25, g->named.action(g->named.ctx, mine, "hipnotic:proximity-watch"));
}
bool qa_q1mp_fire_hipnotic_mjolnir(qa_q1mp_game *game, qa_q1mp_player_state *player) {
  qa_q1mp_actor *strike = game->base->create(game->base, "hipnotic_hammer_strike");
  strike->owner = QA_Q1MP_SOME(player->actor.id);
  game->base->schedule(game->base, strike, 0.3, game->base->named.action(game->base->named.ctx, strike, "hipnotic:hammer-strike"));
  player->continuous_firing = false; player->weapon_animation_at = game->base->time;
  player->weapon_animation_base = qa_q1mp_count(game, player->actor.id, "q1:ammo/cells") < 30 ? 32 : 38;
  return finish_w(game, player, 0.8, 1, "progs/v_hammer.mdl", 0);
}
static void hammer_damage(qa_q1mp_game *game, qa_actor_id from, qa_vec3 start, qa_vec3 end, double damage, const char *weapon) {
  qa_vec3 delta = qa_q1mp_vsub(end, start), side = {-delta.y * 16, -delta.y * 16, 0};
  qa_vec3 offsets[3] = {QA_Q1MP_ZERO, side, qa_q1mp_vscale(side, -1)}; qa_actor_id hit[8]; size_t hits = 0;
  for (int i = 0; i < 3; i++) {
    qa_q1b_trace trace = qa_q1mp_trace(game, qa_q1mp_vadd(start, offsets[i]), qa_q1mp_vadd(end, offsets[i]), QA_Q1MP_SOME(from), true, false);
    if (!trace.actor.has) continue;
    bool seen = false;
    for (size_t j = 0; j < hits; j++) if (qa_same_actor(hit[j], trace.actor.id)) seen = true;
    if (seen) continue;
    if (hits < 8) hit[hits++] = trace.actor.id;
    qa_q1mp_player_state *tp = game->base->player(game->base, trace.actor.id);
    if (qa_q1mp_can_damage_actor(game, trace.actor.id) && (tp ? qa_q1b_powerups_get(tp, "hipnotic:wetsuit") : 0) == 0) {
      if (game->emit_particles) game->emit_particles(game->ctx, trace.end, QA_Q1MP_V3(0,0,100), 225, damage * 4);
      qa_q1mp_damage_full(game, trace.actor.id, from, QA_Q1MP_SOME(from), damage, weapon, "direct", "electric", NULL);
    }
  }
}
void qa_q1mp_spawn_hipnotic_hammer_base(qa_q1mp_game *game, qa_q1mp_player_state *player, qa_vec3 origin, const char *weapon) {
  if (!weapon) weapon = "hipnotic:mjolnir";
  qa_q1b_game *g = game->base; qa_q1mp_actor *base = g->create(g, "hipnotic_mjolnir_base");
  base->owner = QA_Q1MP_SOME(player->actor.id); base->movedir = qa_q1mp_vectors(game, player->view_angles).forward; base->projectile_weapon = weapon;
  g->set_origin(g, base, origin); g->schedule(g, base, 1, g->named.action(g->named.ctx, base, "SUB_Remove"));
  g->sound(g, base, "hipweap/mjolslap.wav", QA_Q1MP_CH("auto"), 1, 1);
  g->sound(g, base, "hipweap/mjolhit.wav", QA_Q1MP_CH("weapon"), 1, 1);
  for (int i = 0; i < 4; i++) {
    qa_q1mp_actor *bolt = g->create(g, "hipnotic_mjolnir_lightning");
    bolt->owner = QA_Q1MP_SOME(base->actor.id); bolt->activator = QA_Q1MP_SOME(player->actor.id); bolt->projectile_weapon = weapon;
    bolt->delay = qa_q1mp_fround(g->time + 0.8); bolt->mangle = QA_Q1MP_V3(0, player->view_angles.y, 0);
    g->set_origin(g, bolt, origin); g->schedule(g, bolt, 0, g->named.action(g->named.ctx, bolt, "hipnotic:hammer-lightning"));
  }
}
static void hammer_strike(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *strike) {
  qa_q1mp_game *game = ((hw *)vctx)->game; qa_q1b_game *g = runtime;
  qa_q1mp_player_state *player = strike->owner.has ? g->player(g, strike->owner.id) : NULL;
  qa_q1b_body body; bool has = player && qa_q1mp_body_read(game, player->actor.id, &body);
  if (!player || !has) { g->remove(g, strike); return; }
  qa_q1b_basis basis = qa_q1mp_vectors(game, player->view_angles);
  qa_vec3 source = qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,16));
  qa_q1b_trace trace = qa_q1mp_trace(game, source, qa_q1mp_vadd(source, qa_q1mp_vscale(basis.forward, 32)), QA_Q1MP_SOME(player->actor.id), true, false);
  double cells = qa_q1mp_count(game, player->actor.id, "q1:ammo/cells");
  player->attack_finished = qa_q1mp_fround(g->time + qa_q1mp_attack_delay(game, player, 0.4));
  if (trace.fraction == 1 && cells >= 15) {
    qa_vec3 start = qa_q1mp_vadd(source, qa_q1mp_vscale(basis.forward, 32));
    trace = qa_q1mp_trace(game, start, qa_q1mp_vsub(start, qa_q1mp_vscale(basis.up, 50)), QA_Q1MP_SOME(player->actor.id), true, false);
    if (trace.fraction > 0.3 && trace.fraction < 1) {
      if (player->water_level > 1) {
        (void)qa_q1mp_consume(game, player->actor, "q1:ammo/cells", cells);
        g->radius_damage(g, player->actor.id, QA_Q1MP_SOME(player->actor.id), 35 * cells, QA_Q1MP_NONE, "hipnotic:mjolnir", "discharge");
      } else { (void)qa_q1mp_consume(game, player->actor, "q1:ammo/cells", 15); qa_q1mp_spawn_hipnotic_hammer_base(game, player, trace.end, NULL); }
      player->attack_finished = qa_q1mp_fround(g->time + qa_q1mp_attack_delay(game, player, 1.5)); g->remove(g, strike); return;
    }
  }
  qa_vec3 origin = qa_q1mp_vsub(trace.end, qa_q1mp_vscale(basis.forward, 4));
  if (trace.actor.has && qa_q1mp_can_damage_actor(game, trace.actor.id)) {
    double damage = qa_q1mp_streq(g->host->classname(g->host->ctx, trace.actor.id), "monster_zombie") ? 70 : 50;
    qa_q1mp_effect_a(game, "blood", origin, trace.actor, damage);
    qa_q1mp_damage_w(game, trace.actor.id, player->actor.id, QA_Q1MP_SOME(player->actor.id), damage, "hipnotic:mjolnir");
  } else if (trace.fraction != 1) { g->sound_owned(g, player->actor, "hipweap/mjoltink.wav", QA_Q1MP_CH("weapon"), 1, 1); qa_q1mp_effect_a(game, "gunshot", origin, QA_Q1MP_NONE, 0); }
  else g->sound_owned(g, player->actor, "knight/sword1.wav", QA_Q1MP_CH("weapon"), 1, 1);
  g->remove(g, strike);
}
static void hammer_lightning(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *bolt) {
  qa_q1mp_game *game = ((hw *)vctx)->game; qa_q1b_game *g = runtime;
  qa_q1mp_actor *base = g->entity(g, bolt->owner);
  if (g->time > bolt->delay || !base || !bolt->activator.has) { g->remove(g, bolt); return; }
  qa_vec3 origin = g->body(g, base).origin; double old_state = bolt->count;
  qa_q1mp_opt_actor target = qa_q1mp_mission_reference(game, bolt, "hipnotic:enemy");
  if (bolt->count == 0) {
    target = QA_Q1MP_NONE; double best = 350;
    qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512);
    for (size_t idx = n; idx-- > 0;) {
      qa_actor_id actor = ents[idx]->actor.id; qa_q1b_body body;
      if (!qa_q1mp_body_read(game, actor, &body) || qa_same_actor(actor, bolt->activator.id) || g->health(g, actor) <= 0 || (ents[idx]->movement_flags & 128) || !(g->is_player(g, QA_Q1MP_SOME(actor)) || ents[idx]->monster)) continue;
      bool struck = false;
      for (size_t j = 0; j < n; j++) if ((qa_q1mp_streq(ents[j]->classname,"hipnotic_mjolnir_lightning")||qa_q1mp_streq(ents[j]->classname,"hipnotic_tesla_lightning")) && ents[j]->count == 1) {
        qa_q1mp_opt_actor s = qa_q1mp_mission_reference(game, ents[j], "hipnotic:enemy"); if (s.has && qa_same_actor(s.id, actor)) struck = true;
      }
      if (struck) continue;
      double dist = qa_q1mp_length(qa_q1mp_vsub(body.origin, origin)); if (dist >= best) continue;
      qa_q1b_trace vis = qa_q1mp_trace(game, origin, body.origin, QA_Q1MP_SOME(bolt->actor.id), false, false);
      if (vis.fraction != 1 || (vis.in_open && vis.in_water)) continue;
      best = dist; target = QA_Q1MP_SOME(actor);
    }
    qa_q1mp_set_mission_reference(bolt, "hipnotic:enemy", target);
    if (!target.has) {
      qa_q1b_basis basis = qa_q1mp_vectors(game, bolt->mangle);
      qa_vec3 end = qa_q1mp_vadd(qa_q1mp_vadd(origin, qa_q1mp_vscale(basis.forward, 200)), qa_q1mp_vscale(basis.right, 400 * qa_q1mp_random(game) - 200));
      qa_q1b_trace trace = qa_q1mp_trace(game, origin, end, QA_Q1MP_SOME(bolt->actor.id), false, false);
      if (game->emit_beam) game->emit_beam(game->ctx, "lightning2", bolt->actor.id, origin, trace.end);
      g->schedule(g, bolt, 0.1, g->named.action(g->named.ctx, bolt, "hipnotic:hammer-lightning")); return;
    }
    bolt->count = 1;
  }
  qa_q1b_body body; bool has = target.has && qa_q1mp_body_read(game, target.id, &body);
  if (!has || !target.has) { bolt->count = 0; g->schedule(g, bolt, 0.1, g->named.action(g->named.ctx, bolt, "hipnotic:hammer-lightning")); return; }
  qa_vec3 size = qa_q1mp_vsub(body.bounds.max, body.bounds.min);
  qa_vec3 end = qa_q1mp_vadd(qa_q1mp_vadd(body.origin, body.bounds.min), qa_q1mp_vscale(size, 0.25 + qa_q1mp_random(game) * 0.5));
  qa_q1b_trace trace = qa_q1mp_trace(game, origin, end, bolt->activator, false, false);
  if (trace.fraction != 1 || g->health(g, target.id) <= 0) { bolt->count = 0; g->schedule(g, bolt, 0.1, g->named.action(g->named.ctx, bolt, "hipnotic:hammer-lightning")); return; }
  if (game->emit_beam) game->emit_beam(game->ctx, "lightning2", bolt->actor.id, origin, trace.end);
  double damage = old_state == 0 ? 80 : 30;
  double facing = qa_q1mp_dot(qa_q1mp_normalize(qa_q1mp_vsub(body.origin, origin)), base->movedir);
  hammer_damage(game, bolt->activator.id, origin, trace.end, facing > 0.3 ? damage : damage * 0.5, bolt->projectile_weapon ? bolt->projectile_weapon : "hipnotic:mjolnir");
  g->schedule(g, bolt, 0.2, g->named.action(g->named.ctx, bolt, "hipnotic:hammer-lightning"));
}
static void laser_think(void *vctx, qa_q1b_game *runtime, qa_q1b_actor *laser) {
  qa_q1mp_game *game = ((hw *)vctx)->game;
  if (runtime->time > laser->attack_finished) { runtime->remove(runtime, laser); return; }
  if (!(game->controls_trajectory && game->controls_trajectory(game->ctx, laser->actor.id))) qa_q1mp_move_missile(game, laser, laser->movedir);
  runtime->schedule(runtime, laser, 0.1, runtime->named.action(runtime->named.ctx, laser, "hipnotic:laser-think"));
}
static void proximity_explode_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e) { (void)r; proximity_explode_impl(((hw *)vctx)->game, e); }
static void proximity_arm_cb(void *vctx, qa_q1b_game *r, qa_q1b_actor *e, qa_q1mp_opt_actor a) { (void)r; (void)a; arm_explosion(((hw *)vctx)->game, e, 0.1); }
/* WeakSet registrations: one wrapper game tracked statically per kind. */
static qa_q1mp_game *laser_registered, *hammer_registered;
void qa_q1mp_register_hipnotic_laser_callbacks(qa_q1mp_game *game) {
  if (laser_registered == game) return;
laser_registered = game; hw *c = hw_new(game);
  qa_q1b_handlers h = {0}; h.ctx = c; h.touch = laser_touch;
  game->base->named.register_named(game->base->named.ctx, "hipnotic:laser-touch", &h);
  qa_q1b_handlers h2 = {0}; h2.ctx = c; h2.action = laser_think;
  game->base->named.register_named(game->base->named.ctx, "hipnotic:laser-think", &h2);
}
void qa_q1mp_register_hipnotic_hammer_callbacks(qa_q1mp_game *game) {
  if (hammer_registered == game) return;
hammer_registered = game; hw *c = hw_new(game);
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = hammer_strike;
  game->base->named.register_named(game->base->named.ctx, "hipnotic:hammer-strike", &h);
  qa_q1b_handlers h2 = {0}; h2.ctx = c; h2.action = hammer_lightning;
  game->base->named.register_named(game->base->named.ctx, "hipnotic:hammer-lightning", &h2);
}
void qa_q1mp_register_hipnotic_weapon_callbacks(qa_q1mp_game *game) {
  qa_q1mp_register_hipnotic_laser_callbacks(game); hw *c = hw_new(game);
  qa_q1b_handlers h = {0}; h.ctx = c; h.touch = proximity_touch;
  game->base->named.register_named(game->base->named.ctx, "hipnotic:proximity-touch", &h);
  qa_q1b_handlers h2 = {0}; h2.ctx = c; h2.action = proximity_watch;
  game->base->named.register_named(game->base->named.ctx, "hipnotic:proximity-watch", &h2);
  qa_q1b_handlers h3 = {0}; h3.ctx = c; h3.action = proximity_explode_cb;
  game->base->named.register_named(game->base->named.ctx, "hipnotic:proximity-explode", &h3);
  qa_q1b_handlers h4 = {0}; h4.ctx = c; h4.die = proximity_arm_cb;
  game->base->named.register_named(game->base->named.ctx, "hipnotic:proximity-arm-explosion", &h4);
  qa_q1mp_register_hipnotic_hammer_callbacks(game);
}
