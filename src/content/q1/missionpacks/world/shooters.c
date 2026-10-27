/* Port of src/content/q1/missionpacks/world/shooters.ts.
 * launchDragonFireball (TS owner: missionpacks/monsters/dragon.ts,
 * landed on qa_q1mpm_* mirror) is the ext below. Projectiles via
 * landed qa/content/q1/base/projectiles.h. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/shooters.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/base/projectiles.h"
#include "qa/content/q1/missionpacks/rogue_weapons.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include <stdlib.h>

qa_q1mp_actor *qa_q1mp_ext_launch_dragon_fireball(qa_q1mp_game *game, qa_actor_id owner, qa_vec3 origin, qa_vec3 direction);
typedef struct sh_ctx { qa_q1mp_game *game; qa_q1mp_pack pack; } sh_ctx;
static void replace_spawn(qa_q1mp_game *game, const char *classname, qa_q1b_spawn_fn fn, void *ctx) {
  if (game->replace_spawn) game->replace_spawn(game->ctx, classname, fn, ctx);
  else game->base->register_spawn(game->base, classname, fn, ctx);
}
static void laser_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; qa_q1mp_game *game = ((sh_ctx *)vctx)->game;
  if (e->owner.has && qa_same_actor(e->owner.id, other)) return;
  qa_q1b_body body = g->body(g, e);
  if (qa_q1mp_streq(g->host->contents(g->host->ctx, body.origin), "sky")) { g->remove(g, e); return; }
  if ((e->spawnflags & 16) == 0) g->sound(g, e, "enforcer/enfstop.wav", QA_Q1MP_CH("weapon"), 3, 1);
  qa_vec3 origin = qa_q1mp_vsub(body.origin, qa_q1mp_vscale(qa_q1mp_normalize(body.velocity), 8));
  if (g->health(g, other) != 0) { qa_q1mp_effect_a(game, "blood", origin, QA_Q1MP_SOME(other), 15); qa_q1b_game_damage(g, other, e->actor.id, e->owner, 15); }
  else qa_q1mp_effect_a(game, "gunshot", origin, QA_Q1MP_NONE, 0);
  g->remove(g, e);
}
static void fire(sh_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  qa_q1mp_game *game = c->game; qa_vec3 origin = g->body(g, e).origin;
  if (qa_q1mp_streq(c->pack, "rogue")) {
    if ((e->spawnflags & 1) != 0 || e->spawnflags == 0) {
      g->sound(g, e, "weapons/spike2.wav", QA_Q1MP_CH("voice"), 1, 1);
      (void)qa_q1_launch_spike(g, QA_Q1MP_SOME(e->actor.id), origin, qa_q1mp_vscale(e->movedir, 500), (e->spawnflags & 1) != 0 ? "superspike" : "spike");
    } else if ((e->spawnflags & 2) != 0) {
      g->sound(g, e, "enforcer/enfire.wav", QA_Q1MP_CH("voice"), 1, 1);
      (void)qa_q1_launch_laser(g, QA_Q1MP_SOME(e->actor.id), origin, e->movedir);
    } else if ((e->spawnflags & 32) != 0) (void)qa_q1mp_ext_launch_dragon_fireball(game, e->actor.id, origin, e->movedir);
    else {
      g->sound(g, e, "weapons/spike2.wav", QA_Q1MP_CH("voice"), 1, 1);
      qa_q1mp_actor *spike = qa_q1mp_launch_rogue_lava_spike(game, e->actor.id, origin, e->movedir, (e->spawnflags & 8) != 0 || ((e->spawnflags & 16) != 0 && g->options.skill > 1));
      qa_q1mp_set_velocity(game, spike, qa_q1mp_vscale(e->movedir, 500));
    }
    return;
  }
  bool audible = (e->spawnflags & 16) == 0;
  if ((e->spawnflags & 2) != 0) {
    if (audible) g->sound(g, e, "enforcer/enfire.wav", QA_Q1MP_CH("voice"), 1, 1);
    qa_q1mp_actor *laser = qa_q1_launch_laser(g, QA_Q1MP_SOME(e->actor.id), origin, e->movedir);
    laser->movement = "fly"; laser->spawnflags = e->spawnflags;
    laser->touch = g->named.touch(g->named.ctx, laser, "hip:shooter_laser_touch");
  } else if ((e->spawnflags & 4) != 0) {
    if (audible) g->sound(g, e, "misc/spike.wav", QA_Q1MP_CH("voice"), 1, 1);
    qa_q1mp_actor *ball = qa_q1_create_missile(g, QA_Q1MP_SOME(e->actor.id), "lavaball", "lavarock", origin, qa_q1mp_vscale(e->movedir, 300));
    ball->projectile = "rocket"; ball->touch = g->named.touch(g->named.ctx, ball, "projectile_touch");
    ball->angular_velocity = QA_Q1MP_V3(0,0,400); g->set_bounds(g, ball, (qa_bounds){{-4,-4,-4},{4,4,4}});
  } else if ((e->spawnflags & 8) != 0) {
    if (audible) g->sound(g, e, "weapons/sgun1.wav", QA_Q1MP_CH("voice"), 1, 1);
    g->sound(g, e, "weapons/sgun1.wav", QA_Q1MP_CH("weapon"), 1, 1);
    qa_q1mp_actor *rocket = qa_q1_create_missile(g, QA_Q1MP_SOME(e->actor.id), "missile", "missile", qa_q1mp_vadd(origin, qa_q1mp_vscale(e->movedir, 8)), qa_q1mp_vscale(e->movedir, 1000));
    rocket->projectile = "rocket"; rocket->touch = g->named.touch(g->named.ctx, rocket, "projectile_touch");
  } else {
    if (audible) g->sound(g, e, "weapons/spike2.wav", QA_Q1MP_CH("voice"), 1, 1);
    (void)qa_q1_launch_spike(g, QA_Q1MP_SOME(e->actor.id), origin, qa_q1mp_vscale(e->movedir, 500), (e->spawnflags & 1) != 0 ? "superspike" : "spike");
  }
}
static void fire_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)o; (void)a; fire(vctx, g, e); }
static void think_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  sh_ctx *c = vctx;
  if (qa_q1mp_streq(c->pack,"rogue") || qa_q1mp_number(e, "shooter_state") != 0) fire(c, g, e);
  qa_q1mp_world_later(c->game, e, e->wait, "mission:shooter_think");
}
static void switch_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)vctx; (void)g; (void)o; (void)a; qa_q1mp_world_number(e, "shooter_state", 1 - qa_q1mp_number(e, "shooter_state")); }
static void shooter_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  sh_ctx *c = vctx;
  e->movedir = qa_q1b_move_direction(g, g->body(g, e).angles);
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = QA_Q1MP_ZERO; g->set_body(g, e, &p);
  e->use = g->named.use(g->named.ctx, e, "mission:shooter_fire");
  if (qa_q1mp_streq(e->classname, "trap_spikeshooter")) return;
  if (e->wait == 0) e->wait = 1;
  qa_q1mp_world_number(e, "shooter_state", qa_q1mp_streq(e->classname,"trap_shooter") ? 1 : qa_q1mp_number(e, "state"));
  if (qa_q1mp_streq(e->classname, "trap_switched_shooter")) e->use = g->named.use(g->named.ctx, e, "mission:shooter_switch");
  qa_q1mp_world_later(c->game, e, qa_q1mp_number(e, "nextthink") + e->wait + qa_q1mp_number(e, "ltime") - g->time, "mission:shooter_think");
}
static void push_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) { (void)vctx; (void)g; (void)o; (void)a; e->spawnflags ^= 4; }
static void push_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; qa_q1mp_game *game = ((sh_ctx *)vctx)->game;
  if ((e->spawnflags & 4) == 0) return;
  const char *cn = g->host->classname(g->host->ctx, other);
  bool grenade = qa_q1mp_streq(cn,"grenade")||qa_q1mp_streq(cn,"MiniGrenade")||qa_q1mp_streq(cn,"MultiGrenade");
  if (grenade || g->health(g, other) > 0) {
    qa_owned_actor owner; qa_q1b_body body;
    bool has_o = g->host->actors_resolve_owned(g->host->ctx, other, &owner), has_b = qa_q1mp_body_read(game, other, &body);
    if (has_o && has_b) { body.velocity = qa_q1mp_vscale(e->movedir, e->speed * 10); g->host->bodies_write(g->host->ctx, owner, &body); }
    if (!grenade && g->is_player(g, QA_Q1MP_SOME(other))) {
      qa_q1mp_actor *ents[256]; size_t total = g->entities(g, ents, 256);
      for (size_t i = 0; i < total; i++) if (qa_q1mp_streq(ents[i]->classname,"rogue_team_state") && ents[i]->owner.has && qa_same_actor(ents[i]->owner.id, other)) {
        if (qa_q1mp_number(ents[i], "fly_sound") < g->time && has_o) {
          qa_q1mp_world_number(ents[i], "fly_sound", g->time + 1.5);
          g->sound_owned(g, owner, "ambience/windfly.wav", QA_Q1MP_CH("auto"), 1, 1);
        }
        break;
      }
    }
  }
  if ((e->spawnflags & 1) != 0) g->remove(g, e);
}
static void push_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  sh_ctx *c = vctx;
  if ((e->spawnflags & 2) != 0) e->use = g->named.use(g->named.ctx, e, "rogue:push");
  else e->spawnflags += 4;
  e->touch = g->named.touch(g->named.ctx, e, "rogue:push");
  if (e->speed == 0) e->speed = 1000;
  qa_q1mp_world_trigger(c->game, e);
}
void qa_q1mp_register_mission_shooters(qa_q1mp_game *game, qa_q1mp_pack pack) {
  sh_ctx *c = malloc(sizeof *c); c->game = game; c->pack = pack; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.touch = laser_touch;
  g->named.register_named(g->named.ctx, "hip:shooter_laser_touch", &h);
  qa_q1b_handlers hf = {0}; hf.ctx = c; hf.use = fire_use;
  g->named.register_named(g->named.ctx, "mission:shooter_fire", &hf);
  qa_q1b_handlers ht = {0}; ht.ctx = c; ht.action = think_cb;
  g->named.register_named(g->named.ctx, "mission:shooter_think", &ht);
  qa_q1b_handlers hs = {0}; hs.ctx = c; hs.use = switch_use;
  g->named.register_named(g->named.ctx, "mission:shooter_switch", &hs);
  replace_spawn(game, "trap_spikeshooter", shooter_spawn, c);
  replace_spawn(game, "trap_shooter", shooter_spawn, c);
  if (qa_q1mp_streq(pack, "hipnotic")) g->register_spawn(g, "trap_switched_shooter", shooter_spawn, c);
  if (qa_q1mp_streq(pack, "rogue")) {
    qa_q1b_handlers hp = {0}; hp.ctx = c; hp.use = push_use; hp.touch = push_touch;
    g->named.register_named(g->named.ctx, "rogue:push", &hp);
    g->register_spawn(g, "trigger_push", push_spawn, c);
  }
}
