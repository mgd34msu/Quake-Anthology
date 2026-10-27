/* Port of src/content/q1/missionpacks/world/rogue-pendulum.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_pendulum.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/foundation/support.h"
#include <stdio.h>
#include <stdlib.h>

static const double SWINGS[][8] = {
  {0,-176,-120,48,128,0.17,1,0},{1,-172,-112,12,88,0.15,0,0},{2,-160,-96,-22,50,0.13,0,0},
  {3,-138,-70,-51,17,0.11,0,1},{4,-110,-38,-72,-8,0.09,0,0},{5,-76,0,-83,-23,0.07,0,0},
  {6,-40,40,-88,-32,0.05,0,0},{7,0,76,-83,-23,0.07,0,0},{8,38,100,-72,-8,0.09,0,0},
  {9,70,138,-51,17,0.11,0,0},{10,96,160,-22,50,0.13,0,0},{11,112,172,12,88,0.15,0,0},
  {12,120,176,48,128,0.17,0,0},{12,120,176,48,128,0.17,0,0},{11,112,172,12,88,0.15,-1,0},
  {10,96,160,-22,50,0.13,0,0},{9,70,138,-51,17,0.11,0,1},{8,38,100,-72,-8,0.09,0,0},
  {7,0,76,-83,-23,0.07,0,0},{6,-40,40,-88,-32,0.05,0,0},{5,-76,0,-83,-23,0.07,0,0},
  {4,-110,-28,-72,-8,0.09,0,0},{3,-172,-70,-51,17,0.11,0,0},{2,-160,-96,-22,50,0.13,0,0},
  {1,-172,-112,12,88,0.15,0,0},{0,-176,-120,48,128,0.17,0,0},
};
#define SWING_COUNT 26
typedef struct pend_ctx { qa_q1mp_game *game; int index; } pend_ctx;
static pend_ctx swing_ctxs[SWING_COUNT];
static void swing_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  pend_ctx *c = vctx; int index = c->index;
  const double *s = SWINGS[index];
  e->frame = s[0];
  if (index != 13) {
    qa_bounds b = (e->spawnflags & 2) != 0
      ? (qa_bounds){{-8, s[1], s[3]}, {8, s[2], s[4]}}
      : (qa_bounds){{s[1], -8, s[3]}, {s[2], 8, s[4]}};
    g->set_bounds(g, e, b);
  }
  if (s[6] != 0 && g->world) qa_q1mp_world_number(g->world, "rogue:impactVelocity", s[6]);
  if (s[7] != 0 && c->game->emit_sound_event) c->game->emit_sound_event(c->game->ctx, e->actor.id, "pendulum/swing.wav", QA_Q1MP_CH("auto"), 0.5, 1);
  char name[48]; (void)snprintf(name, sizeof name, "rogue:pend_swing%d", (index + 1) % SWING_COUNT + 1);
  qa_q1mp_world_later(c->game, e, s[5], name);
}
static void pend_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; qa_q1mp_game *game = ((pend_ctx *)vctx)->game;
  if (g->health(g, other) < 1 || !qa_q1mp_can_damage_actor(game, other)) return;
  if (e->attack_finished < g->time) { qa_q1mp_sound_ch(game, e, "pendulum/hit.wav", "voice"); e->attack_finished = g->time + 1; }
  qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(e->actor.id), qa_q1mp_number(e, "currentammo"));
  qa_q1b_body body; qa_owned_actor owner;
  if (!qa_q1mp_body_read(game, other, &body) || !g->host->actors_resolve_owned(g->host->ctx, other, &owner)) return;
  double impact = g->world ? qa_q1mp_number(g->world, "rogue:impactVelocity") : 0;
  if ((e->spawnflags & 2) != 0) { body.velocity.y = impact * -250; body.velocity.z = 200; }
  else { body.velocity.x = impact * 250; body.velocity.z = 200; }
  g->host->bodies_write(g->host->ctx, owner, &body);
  qa_q1mp_effect_a(game, "meat-spray", body.origin, QA_Q1MP_SOME(other), 0);
}
static void pend_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)g; (void)o; (void)a; qa_q1mp_world_later(((pend_ctx *)vctx)->game, e, e->delay, "rogue:pend_swing1");
}
static void pendulum_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  pend_ctx *c = vctx;
  qa_q1mp_set_model(e, "progs/pendulum.mdl");
  if (e->spawnflags == 0) e->spawnflags = 2;
  if ((e->spawnflags & 3) == 0) qa_range_error("Unimplemented Pendulum Type (pendulum.qc)");
  bool across = (e->spawnflags & 2) != 0;
  qa_q1b_body_patch p = {0};
  p.has_angles = true; p.angles = across ? QA_Q1MP_ZERO : QA_Q1MP_V3(0,270,0);
  p.has_bounds = true; p.bounds = across ? (qa_bounds){{-8,-24,-100},{8,24,100}} : (qa_bounds){{-24,-8,-100},{24,8,100}};
  qa_q1mp_patch(c->game, e, &p);
  if (qa_q1mp_number(e, "currentammo") == 0) qa_q1mp_world_number(e, "currentammo", 5);
  if (e->delay == 0) e->delay = 1;
  e->solid = "trigger"; qa_q1b_actor_set_damageable(g, e, false);
  e->touch = g->named.touch(g->named.ctx, e, "rogue:pend_touch");
  if (g->world) qa_q1mp_world_number(g->world, "rogue:impactVelocity", 0);
  if ((e->spawnflags & 8) != 0) e->use = g->named.use(g->named.ctx, e, "rogue:pend_use");
  else qa_q1mp_world_later(c->game, e, e->delay, "rogue:pend_swing1");
}
void qa_q1mp_register_rogue_pendulum(qa_q1mp_game *game) {
  qa_q1b_game *g = game->base;
  for (int i = 0; i < SWING_COUNT; i++) {
    swing_ctxs[i].game = game; swing_ctxs[i].index = i;
    char name[48]; (void)snprintf(name, sizeof name, "rogue:pend_swing%d", i + 1);
    char *stored = malloc(48); (void)snprintf(stored, 48, "%s", name);
    qa_q1b_handlers h = {0}; h.ctx = &swing_ctxs[i]; h.action = swing_cb;
    g->named.register_named(g->named.ctx, stored, &h);
  }
  pend_ctx *shared = malloc(sizeof *shared); shared->game = game; shared->index = 0;
  qa_q1b_handlers ht = {0}; ht.ctx = shared; ht.touch = pend_touch;
  g->named.register_named(g->named.ctx, "rogue:pend_touch", &ht);
  qa_q1b_handlers hu = {0}; hu.ctx = shared; hu.use = pend_use;
  g->named.register_named(g->named.ctx, "rogue:pend_use", &hu);
  g->register_spawn(g, "pendulum", pendulum_spawn, shared);
}
