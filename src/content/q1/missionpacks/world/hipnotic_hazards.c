/* Port of src/content/q1/missionpacks/world/hipnotic-hazards.ts.
 * Observation scans use game entities reversed (see
 * hipnotic_weapons.c note). GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_hazards.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>

typedef struct hz_ctx { qa_q1mp_game *game; } hz_ctx;
bool qa_q1mp_is_struck_by_mjolnir(qa_q1mp_game *game, qa_actor_id actor) {
  qa_q1mp_actor *ents[512]; size_t n = game->base->entities(game->base, ents, 512);
  for (size_t i = 0; i < n; i++) {
    if (!(qa_q1mp_streq(ents[i]->classname,"hipnotic_mjolnir_lightning")||qa_q1mp_streq(ents[i]->classname,"hipnotic_tesla_lightning"))) continue;
    if (ents[i]->count != 1) continue;
    qa_q1mp_opt_actor enemy = qa_q1b_references_get(ents[i], "hipnotic:enemy");
    /* TS checks references.get !== undefined && !== null. */
    bool present = false;
    for (size_t j = 0; j < ents[i]->reference_count; j++) if (qa_q1mp_streq(ents[i]->references[j].key, "hipnotic:enemy")) present = true;
    if (present && enemy.has && qa_same_actor(actor, enemy.id)) return true;
  }
  return false;
}
static bool visible_to(hz_ctx *c, qa_q1mp_actor *entity, qa_actor_id actor) {
  qa_q1b_game *g = c->game->base; qa_q1b_body body;
  if (!qa_q1mp_body_read(c->game, actor, &body)) return false;
  qa_q1mp_actor *target = g->entity(g, QA_Q1MP_SOME(actor));
  double z = g->is_player(g, QA_Q1MP_SOME(actor)) ? 22 : (target ? qa_q1mp_vector_of(target, "view_ofs").z : 0);
  qa_q1b_trace trace = qa_q1mp_trace(c->game, qa_q1mp_vadd(g->body(g, entity).origin, qa_q1mp_vector_of(entity, "view_ofs")),
    qa_q1mp_vadd(body.origin, QA_Q1MP_V3(0,0,z)), QA_Q1MP_SOME(entity->actor.id), false, false);
  return trace.fraction == 1 && !(trace.in_open && trace.in_water);
}
static size_t scan_actors(hz_ctx *c, qa_q1mp_actor *entity, double radius, bool include_monsters, qa_actor_id *out, size_t capacity) {
  qa_q1b_game *g = c->game->base; qa_vec3 origin = g->body(g, entity).origin;
  qa_q1mp_actor *ents[512]; size_t n = g->entities(g, ents, 512); size_t found = 0;
  for (size_t idx = n; idx-- > 0 && found < capacity;) {
    qa_actor_id actor = ents[idx]->actor.id; qa_q1mp_actor *target = ents[idx]; qa_q1b_body body;
    if (!qa_q1mp_body_read(c->game, actor, &body)) continue;
    if ((target->movement_flags & 128) != 0) continue;
    if (!g->is_player(g, QA_Q1MP_SOME(actor)) && (!include_monsters || (target->movement_flags & 32) == 0)) continue;
    qa_vec3 center = qa_q1mp_vadd(body.origin, qa_q1mp_vscale(qa_q1mp_vadd(body.bounds.min, body.bounds.max), 0.5));
    if (qa_q1mp_length(qa_q1mp_vsub(center, origin)) <= radius && g->health(g, actor) > 0 && visible_to(c, entity, actor)) out[found++] = actor;
  }
  return found;
}
static void lightning_damage(hz_ctx *c, qa_q1mp_actor *entity, qa_vec3 start, qa_vec3 end, qa_q1mp_opt_actor from, double amount) {
  qa_q1b_game *g = c->game->base;
  qa_q1mp_opt_actor inflictor = from.has ? from : (g->world ? QA_Q1MP_SOME(g->world->actor.id) : QA_Q1MP_NONE);
  if (!inflictor.has) qa_range_error("Lightning damage requires worldspawn");
  qa_vec3 side = {-(end.y-start.y)*16, -(end.y-start.y)*16, 0};
  qa_vec3 offsets[3] = {QA_Q1MP_ZERO, side, qa_q1mp_vscale(side,-1)}; qa_actor_id hit[8]; size_t hits = 0;
  for (int i = 0; i < 3; i++) {
    qa_q1b_trace trace = qa_q1mp_trace(c->game, qa_q1mp_vadd(start,offsets[i]), qa_q1mp_vadd(end,offsets[i]), QA_Q1MP_SOME(entity->actor.id), true, false);
    if (!trace.actor.has) continue;
    bool seen = false;
    for (size_t j = 0; j < hits; j++) if (qa_same_actor(hit[j], trace.actor.id)) seen = true;
    if (seen) continue;
    if (hits < 8) hit[hits++] = trace.actor.id;
    qa_q1mp_player_state *tp = g->player(g, trace.actor.id);
    if (!qa_q1mp_can_damage_actor(c->game, trace.actor.id) || (tp ? qa_q1b_powerups_get(tp,"hipnotic:wetsuit") : 0) != 0) continue;
    if (c->game->emit_particles) c->game->emit_particles(c->game->ctx, trace.end, QA_Q1MP_V3(0,0,100), 225, amount * 4);
    qa_q1mp_damage_full(c->game, trace.actor.id, inflictor.id, from, amount, NULL, "direct", "electric", NULL);
  }
}
static void mine_explode(hz_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  g->radius_damage(g, e->actor.id, QA_Q1MP_SOME(e->actor.id), 110, QA_Q1MP_NONE, NULL, NULL);
  qa_q1mp_sound_ch(c->game, e, "weapons/r_exp3.wav", "weapon");
  qa_q1mp_effect_a(c->game, "explosion", g->body(g, e).origin, QA_Q1MP_NONE, 0);
  qa_q1mp_sound_ch(c->game, e, "misc/null.wav", "voice");
  qa_q1mp_set_velocity(c->game, e, QA_Q1MP_ZERO);
  e->touch.has = false; qa_q1mp_set_model(e, "progs/s_explod.spr"); e->solid = "none"; e->frame = 0;
  g->link(g, e); qa_q1mp_world_later(c->game, e, 0.1, "base:explosion_frame");
}
static void mine_home_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx;
  e->frame = fmod(e->frame + 1, 9);
  qa_q1mp_world_later(c->game, e, 0.2, "hip:mine_home");
  if (qa_q1mp_number(e, "search_time") < g->time) {
    double distance = 2000; qa_q1mp_opt_actor selected = QA_Q1MP_NONE;
    qa_actor_id found[64]; size_t n = scan_actors(c, e, 2000, false, found, 64);
    for (size_t i = 0; i < n; i++) {
      qa_q1b_body body;
      if (!qa_q1mp_body_read(c->game, found[i], &body)) continue;
      double next = qa_q1mp_length(qa_q1mp_vsub(body.origin, g->body(g, e).origin));
      if (next < distance) { distance = next; selected = QA_Q1MP_SOME(found[i]); }
    }
    if (selected.has) qa_q1mp_sound_ch(c->game, e, "hipitems/spikmine.wav", "voice");
    qa_q1b_references_set(e, "enemy", selected);
    qa_q1mp_world_number(e, "search_time", g->time + 1.3);
  }
  qa_q1mp_opt_actor enemy = qa_q1b_references_get(e, "enemy");
  qa_q1b_body target; bool has = enemy.has && qa_q1mp_body_read(c->game, enemy.id, &target);
  if (!has) { qa_q1mp_sound_ch(c->game, e, "misc/null.wav", "voice"); qa_q1mp_set_velocity(c->game, e, QA_Q1MP_ZERO); return; }
  qa_vec3 direction = qa_q1mp_normalize(qa_q1mp_vsub(qa_q1mp_vadd(target.origin, QA_Q1MP_V3(0,0,10)), g->body(g, e).origin));
  bool in_front = qa_q1mp_dot(qa_q1mp_normalize(qa_q1mp_vsub(target.origin, g->body(g, e).origin)), qa_q1mp_vectors(c->game, g->body(g, e).angles).forward) > 0.3;
  qa_q1mp_set_velocity(c->game, e, qa_q1mp_vscale(direction, g->options.skill * 50 + (in_front ? 50 : 150)));
}
static void lightning_think(hz_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  if (g->time > e->delay) { g->remove(g, e); return; }
  qa_vec3 start = g->body(g, e).origin, end = qa_q1mp_vector_of(e, "oldorigin");
  qa_q1mp_opt_actor client = g->host->check_client(g->host->ctx, e->actor);
  if (client.has && c->game->emit_beam) c->game->emit_beam(c->game->ctx, "lightning2", e->actor.id, start, end);
  lightning_damage(c, e, start, end, qa_q1b_references_get(e, "lastvictim"), e->damage);
  qa_q1mp_world_later(c->game, e, 0.1, "hip:lightning_bolt");
}
static void lightning_bolt_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { lightning_think(vctx, g, e); }
static void lightning_use_impl(hz_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  if (g->time >= qa_q1mp_number(e, "pausetime")) {
    qa_q1mp_sound(c->game, e, (e->spawnflags & 2) != 0 ? "weapons/lstart.wav" : "weapons/lhit.wav");
    if (qa_q1mp_streq(e->classname, "trap_lightning_triggered")) qa_q1mp_world_number(e, "pausetime", g->time + 0.1);
  }
  qa_vec3 start = g->body(g, e).origin, end;
  if (e->target[0]) {
    qa_q1mp_opt_actor enemy_ref = qa_q1b_references_get(e, "enemy");
    qa_q1mp_actor *enemy = enemy_ref.has ? g->entity(g, enemy_ref) : NULL;
    if (!enemy) enemy = g->world;
    if (!enemy) qa_range_error("Lightning requires worldspawn");
    end = g->body(g, enemy).origin;
  } else {
    e->movedir = qa_q1mp_vectors(c->game, g->body(g, e).angles).forward;
    end = qa_q1mp_trace(c->game, start, qa_q1mp_vadd(start, qa_q1mp_vscale(e->movedir, 600)), QA_Q1MP_SOME(e->actor.id), false, false).end;
  }
  qa_vec3 direction = qa_q1mp_normalize(qa_q1mp_vsub(end, start));
  double distance = qa_q1mp_length(qa_q1mp_vsub(end, start)) / 30, remainder = distance - floor(distance);
  if (remainder > 0) { start = qa_q1mp_vadd(start, qa_q1mp_vscale(direction, (remainder-1)*15)); end = qa_q1mp_vsub(end, qa_q1mp_vscale(direction, (remainder-1)*15)); }
  if (qa_q1mp_number(e, "duration") > 0.1) {
    qa_q1mp_actor *bolt = g->create(g, "hipnotic_lightning");
    g->set_origin(g, bolt, start); qa_q1mp_world_vector(bolt, "oldorigin", end);
    qa_q1b_references_set(bolt, "lastvictim", QA_Q1MP_SOME(e->actor.id));
    bolt->damage = e->damage; bolt->delay = g->time + qa_q1mp_number(e, "duration");
    lightning_think(c, g, bolt); return;
  }
  qa_q1mp_opt_actor client = g->host->check_client(g->host->ctx, e->actor);
  if (client.has && c->game->emit_beam) c->game->emit_beam(c->game->ctx, "lightning2", e->actor.id, start, end);
  lightning_damage(c, e, start, end, QA_Q1MP_SOME(e->actor.id), e->damage);
}
static void lightning_use_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)o; (void)a; lightning_use_impl(vctx, g, e); }
static size_t tesla_scan(hz_ctx *c, qa_q1mp_actor *e, qa_actor_id *out, size_t cap) {
  qa_actor_id all[128]; size_t n = scan_actors(c, e, qa_q1mp_number(e, "distance"), (e->spawnflags & 1) != 0, all, 128);
  size_t found = 0;
  for (size_t i = 0; i < n && found < cap; i++) {
    if (qa_q1mp_is_struck_by_mjolnir(c->game, all[i])) continue;
    out[found++] = all[i];
    if (found >= (size_t)(e->count > 0 ? e->count : 0) && e->count > 0) break;
  }
  return found;
}
static void tesla_bolt_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx;
  qa_q1mp_actor *owner = g->entity(g, e->owner);
  if (owner) qa_q1mp_world_number(owner, "attack_state", 2);
  qa_q1mp_opt_actor enemy = qa_q1b_references_get(e, "hipnotic:enemy");
  qa_q1b_body target; bool has = enemy.has && qa_q1mp_body_read(c->game, enemy.id, &target);
  if (g->time > e->delay || !has) { g->remove(g, e); return; }
  qa_vec3 start = g->body(g, e).origin;
  qa_q1b_trace trace = qa_q1mp_trace(c->game, start, target.origin, QA_Q1MP_SOME(e->actor.id), false, false);
  if (trace.fraction != 1 || !enemy.has || g->health(g, enemy.id) <= 0 || qa_q1mp_length(qa_q1mp_vsub(start, target.origin)) > qa_q1mp_number(e,"distance") + 10) { g->remove(g, e); return; }
  if (c->game->emit_beam) c->game->emit_beam(c->game->ctx, "lightning2", e->actor.id, start, trace.end);
  lightning_damage(c, e, start, trace.end, qa_q1b_references_get(e, "lastvictim"), e->damage);
  qa_q1mp_world_later(c->game, e, 0.1, "hip:tesla_bolt");
}
static void tesla_think_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx;
  if (qa_q1mp_number(e, "hazard_state") == 0) { qa_q1mp_world_later(c->game, e, 0.25, "hip:tesla_think"); return; }
  double state = qa_q1mp_number(e, "attack_state");
  if (state == 0) {
    qa_actor_id targets[32]; size_t n = tesla_scan(c, e, targets, 32);
    if (n > 0) {
      if (e->wait > 0) qa_q1mp_sound(c->game, e, "misc/tesla.wav");
      qa_q1mp_world_number(e, "attack_state", 1);
      qa_q1mp_world_later(c->game, e, e->wait, "hip:tesla_think"); return;
    }
    if (e->delay > 0 && g->time > qa_q1mp_number(e, "search_time")) qa_q1mp_world_number(e, "attack_state", 3);
    qa_q1mp_world_later(c->game, e, 0.25, "hip:tesla_think"); return;
  }
  if (state == 1) {
    qa_actor_id targets[32]; size_t n = tesla_scan(c, e, targets, 32);
    for (size_t i = 0; i < n; i++) {
      qa_q1mp_sound(c->game, e, "hipweap/mjolhit.wav");
      qa_q1mp_actor *bolt = g->create(g, "hipnotic_tesla_lightning");
      bolt->count = 1; bolt->owner = QA_Q1MP_SOME(e->actor.id);
      qa_q1b_references_set(bolt, "hipnotic:enemy", QA_Q1MP_SOME(targets[i]));
      qa_q1b_references_set(bolt, "lastvictim", qa_q1b_references_get(e, "lastvictim"));
      double dur = qa_q1mp_number(e, "duration");
      bolt->delay = g->time + (dur > 0 ? dur : 9999); bolt->damage = e->damage;
      qa_q1mp_world_number(bolt, "distance", qa_q1mp_number(e, "distance"));
      g->set_origin(g, bolt, g->body(g, e).origin);
      qa_q1mp_world_later(c->game, bolt, 0, "hip:tesla_bolt");
    }
    qa_q1mp_world_number(e, "attack_state", 2);
    qa_q1mp_world_later(c->game, e, 1, "hip:tesla_think"); return;
  }
  if (state == 2) { qa_q1mp_world_number(e, "attack_state", 3); qa_q1mp_world_later(c->game, e, 0.2, "hip:tesla_think"); return; }
  qa_q1mp_world_number(e, "attack_state", 0);
  if (qa_q1mp_streq(e->classname, "trap_gods_wrath")) g->cancel(g, e);
  else qa_q1mp_world_later(c->game, e, 0.1, "hip:tesla_think");
}
static void mine_first_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx; qa_q1mp_world_number(e, "search_time", 0);
  qa_q1b_actor_set_damageable(g, e, true); e->aimed_damage = true;
  e->use = g->named.use(g->named.ctx, e, "hip:mine_use");
  qa_q1mp_world_later(c->game, e, 0.1, "hip:mine_home");
}
static void mine_use_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor activator) {
  (void)o; hz_ctx *c = vctx;
  if (activator.has && g->is_player(g, activator)) {
    qa_q1mp_player_state *p = g->player(g, activator.id);
    if ((p ? qa_q1b_powerups_get(p, "invisibility") : 0) <= g->time) {
      qa_q1b_references_set(e, "enemy", activator);
      qa_q1mp_world_later(c->game, e, 0.1, "hip:mine_home");
    }
  }
}
static void mine_die_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor attacker) {
  hz_ctx *c = vctx; qa_q1b_actor_set_damageable(g, e, false); g->killed_monsters++;
  if (c->game->emit_monster_killed) c->game->emit_monster_killed(c->game->ctx, e->actor.id, g->total_monsters, g->killed_monsters);
  g->use_targets(g, e, attacker); mine_explode(c, g, e);
}
static void mine_touch_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; hz_ctx *c = vctx;
  if (g->health(g, e->actor.id) > 0) {
    const char *cn = g->host->classname(g->host->ctx, other);
    if (qa_q1mp_streq(cn,"trap_spike_mine")||qa_q1mp_streq(cn,"missile")||qa_q1mp_streq(cn,"grenade")||qa_q1mp_streq(cn,"hiplaser")||qa_q1mp_streq(cn,"proximity_grenade")) return;
    qa_q1b_game_damage(g, e->actor.id, e->actor.id, QA_Q1MP_SOME(e->actor.id), g->health(g, e->actor.id) + 10);
  }
  mine_explode(c, g, e);
}
static void mine_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx;
  if (g->options.deathmatch != 0) { g->remove(g, e); return; }
  qa_q1mp_set_model(e, "progs/spikmine.mdl"); e->solid = "bbox"; e->movement = "flymissile";
  e->angular_velocity = QA_Q1MP_V3(-50,100,150);
  e->max_health = g->options.skill <= 1 ? 200 : 400;
  g->host->combat_set_health(g->host->ctx, e->actor, e->max_health);
  e->frame = 0; e->movement_flags |= 32; g->total_monsters++;
  e->touch = g->named.touch(g->named.ctx, e, "hip:mine_explode");
  e->die = g->named.die(g->named.ctx, e, "hip:mine_explode");
  qa_q1mp_world_later(c->game, e, 0.2, "hip:mine_first");
}
static void hazard_switch_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; (void)vctx;
  qa_q1mp_world_number(e, "hazard_state", 1 - qa_q1mp_number(e, "hazard_state"));
  if (qa_q1mp_number(e, "hazard_state") == 1 && e->think.has)
    g->schedule(g, e, qa_q1mp_number(e, "huntingcharmer") - g->time, e->think);
}
static void lightning_think_state_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx;
  if (qa_q1mp_number(e, "hazard_state") != 0) lightning_use_impl(c, g, e);
  if (qa_q1mp_number(e, "cnt") == 0) {
    double delay = (e->spawnflags & 1) != 0 ? e->wait * qa_q1mp_random(c->game) : e->wait;
    qa_q1mp_world_number(e, "cnt", 1);
    qa_q1mp_world_number(e, "t_length", g->time + qa_q1mp_number(e,"duration") - 0.1);
    double pt = g->time + qa_q1mp_number(e,"duration") - 0.1; if (pt < g->time + 0.3) pt = g->time + 0.3;
    qa_q1mp_world_number(e, "pausetime", pt);
    if (delay < qa_q1mp_number(e,"duration")) delay = qa_q1mp_number(e,"duration");
    qa_q1mp_world_number(e, "t_width", g->time + delay);
  }
  if (g->time >= qa_q1mp_number(e, "t_length")) {
    qa_q1mp_world_number(e, "cnt", 0);
    qa_q1mp_world_later(c->game, e, qa_q1mp_number(e,"t_width") - g->time, "hip:lightning_think"); return;
  }
  qa_q1mp_world_later(c->game, e, 0.2, "hip:lightning_think");
}
static void lightning_first_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx;
  if (e->target[0]) {
    qa_q1mp_actor *t = qa_q1mp_find0(c->game, e->target);
    qa_q1b_references_set(e, "enemy", t ? QA_Q1MP_SOME(t->actor.id) : QA_Q1MP_NONE);
  }
  if (qa_q1mp_streq(e->classname, "trap_lightning_triggered")) { g->cancel(g, e); return; }
  qa_q1mp_world_later(c->game, e, qa_q1mp_number(e,"huntingcharmer") + e->wait + qa_q1mp_number(e,"ltime") - g->time, "hip:lightning_think");
}
static void lightning_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx;
  if (e->wait == 0) e->wait = 1;
  if (e->damage == 0) e->damage = 30;
  if (qa_q1mp_number(e,"duration") == 0) qa_q1mp_world_number(e, "duration", 0.1);
  qa_q1mp_world_number(e, "cnt", 0);
  qa_q1mp_world_number(e, "hazard_state", qa_q1mp_streq(e->classname,"trap_lightning") ? 1 : qa_q1mp_number(e,"state"));
  qa_q1mp_world_number(e, "huntingcharmer", qa_q1mp_number(e,"nextthink"));
  e->use = g->named.use(g->named.ctx, e, qa_q1mp_streq(e->classname,"trap_lightning_switched") ? "hip:hazard_switch" : "hip:lightning_use");
  qa_q1mp_world_later(c->game, e, 0.25, "hip:lightning_first");
}
static void wrath_use_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor activator) {
  (void)o; hz_ctx *c = vctx;
  if (qa_q1mp_number(e, "attack_state") != 0) return;
  qa_q1mp_world_number(e, "search_time", g->time + e->delay);
  qa_q1b_references_set(e, "lastvictim", activator);
  tesla_think_cb(c, g, e);
}
static void tesla_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx; bool wrath = qa_q1mp_streq(e->classname, "trap_gods_wrath");
  if (e->wait == 0) e->wait = 2;
  if (e->damage == 0) e->damage = 2 + 5 * g->options.skill;
  if (qa_q1mp_number(e,"duration") == 0) qa_q1mp_world_number(e, "duration", -1);
  if (qa_q1mp_number(e,"distance") == 0) qa_q1mp_world_number(e, "distance", 600);
  if (e->delay == 0) e->delay = wrath ? 5 : -1;
  qa_q1mp_world_number(e, "hazard_state", qa_q1mp_number(e,"state"));
  qa_q1mp_world_number(e, "attack_state", 0);
  qa_q1b_references_set(e, "lastvictim", QA_Q1MP_NONE);
  e->use = g->named.use(g->named.ctx, e, "hip:hazard_switch");
  qa_q1mp_world_later(c->game, e, qa_q1mp_random(c->game), "hip:tesla_think");
  if (wrath) { e->wait = 0; qa_q1mp_world_number(e, "hazard_state", 1); g->cancel(g, e); e->use = g->named.use(g->named.ctx, e, "hip:wrath_use"); }
}
static void gravity_action(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx; qa_q1mp_world_number(e, "ltime", g->time);
  qa_actor_id targets[32]; size_t n = tesla_scan(c, e, targets, 32);
  for (size_t i = 0; i < n; i++) {
    qa_owned_actor owner; qa_q1b_body body;
    if (!g->host->actors_resolve_owned(g->host->ctx, targets[i], &owner) || !qa_q1mp_body_read(c->game, targets[i], &body)) continue;
    qa_q1mp_player_state *tp = g->player(g, targets[i]);
    double scale = ((e->spawnflags & 2) != 0 && (tp ? qa_q1b_powerups_get(tp,"hipnotic:wetsuit") : 0) > g->time) ? 0.6 : 1;
    qa_vec3 pull = qa_q1mp_vscale(qa_q1mp_normalize(qa_q1mp_vsub(g->body(g, e).origin, body.origin)), e->speed * scale);
    body.velocity = qa_q1mp_vadd(body.velocity, pull);
    g->host->bodies_write(g->host->ctx, owner, &body);
  }
  qa_q1mp_world_later(c->game, e, 0.1, "hip:gravity_well");
}
static void gravity_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; hz_ctx *c = vctx;
  if (e->attack_finished > g->time || !qa_q1mp_can_damage_actor(c->game, other)) return;
  qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(e->actor.id), e->damage);
  e->attack_finished = g->time + 0.2;
}
static void gravity_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hz_ctx *c = vctx;
  e->solid = "trigger"; e->movement = "none";
  if (e->damage == 0) e->damage = 10000;
  if (e->speed == 0) e->speed = 210;
  if (qa_q1mp_number(e,"distance") == 0) qa_q1mp_world_number(e, "distance", 600);
  e->touch = g->named.touch(g->named.ctx, e, "hip:gravity_well");
  g->set_bounds(g, e, (qa_bounds){{-16,-16,-16},{16,16,16}});
  qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1mp_world_later(c->game, e, 0.1, "hip:gravity_well");
}
void qa_q1mp_register_hipnotic_hazards(qa_q1mp_game *game) {
  hz_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = mine_home_cb;
  g->named.register_named(g->named.ctx, "hip:mine_home", &h);
  qa_q1b_handlers hf = {0}; hf.ctx = c; hf.action = mine_first_cb;
  g->named.register_named(g->named.ctx, "hip:mine_first", &hf);
  qa_q1b_handlers hu = {0}; hu.ctx = c; hu.use = mine_use_cb;
  g->named.register_named(g->named.ctx, "hip:mine_use", &hu);
  qa_q1b_handlers he = {0}; he.ctx = c; he.die = mine_die_cb; he.touch = mine_touch_cb;
  g->named.register_named(g->named.ctx, "hip:mine_explode", &he);
  g->register_spawn(g, "trap_spike_mine", mine_spawn, c);
  qa_q1b_handlers hl = {0}; hl.ctx = c; hl.action = lightning_bolt_cb;
  g->named.register_named(g->named.ctx, "hip:lightning_bolt", &hl);
  qa_q1b_handlers hlu = {0}; hlu.ctx = c; hlu.use = lightning_use_cb;
  g->named.register_named(g->named.ctx, "hip:lightning_use", &hlu);
  qa_q1b_handlers hsw = {0}; hsw.ctx = c; hsw.use = hazard_switch_use;
  g->named.register_named(g->named.ctx, "hip:hazard_switch", &hsw);
  qa_q1b_handlers hlt = {0}; hlt.ctx = c; hlt.action = lightning_think_state_cb;
  g->named.register_named(g->named.ctx, "hip:lightning_think", &hlt);
  qa_q1b_handlers hlf = {0}; hlf.ctx = c; hlf.action = lightning_first_cb;
  g->named.register_named(g->named.ctx, "hip:lightning_first", &hlf);
  static const char *const lightning[] = {"trap_lightning","trap_lightning_triggered","trap_lightning_switched"};
  for (size_t i = 0; i < 3; i++) g->register_spawn(g, lightning[i], lightning_spawn, c);
  qa_q1b_handlers htb = {0}; htb.ctx = c; htb.action = tesla_bolt_cb;
  g->named.register_named(g->named.ctx, "hip:tesla_bolt", &htb);
  qa_q1b_handlers htt = {0}; htt.ctx = c; htt.action = tesla_think_cb;
  g->named.register_named(g->named.ctx, "hip:tesla_think", &htt);
  qa_q1b_handlers hw = {0}; hw.ctx = c; hw.use = wrath_use_cb;
  g->named.register_named(g->named.ctx, "hip:wrath_use", &hw);
  g->register_spawn(g, "trap_tesla_coil", tesla_spawn, c);
  g->register_spawn(g, "trap_gods_wrath", tesla_spawn, c);
  qa_q1b_handlers hg = {0}; hg.ctx = c; hg.action = gravity_action; hg.touch = gravity_touch;
  g->named.register_named(g->named.ctx, "hip:gravity_well", &hg);
  g->register_spawn(g, "trap_gravity_well", gravity_spawn, c);
}
