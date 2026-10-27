/* Port of src/content/q1/missionpacks/world/rogue-misc.ts.
 * makeStatic (TS owner: base/map-entities.ts) is the ext below.
 * registerDamageSourceEffects routes via the wrapper hook.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/rogue_misc.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/foundation/support.h"
#include <stdlib.h>

void qa_q1mp_ext_make_static(qa_q1mp_game *game, qa_q1mp_actor *entity);
typedef struct rm_ctx { qa_q1mp_game *game; } rm_ctx;
static void torch_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rm_ctx *c = vctx; const char *model = g->precache_model(g, "progs/flame.mdl");
  qa_q1mp_set_model(e, model);
  if ((e->spawnflags & 1) == 0) {
    const char *path = g->precache_sound(g, "ambience/fire1.wav");
    if (c->game->emit_ambient) c->game->emit_ambient(c->game->ctx, g->body(g, e).origin, path, 0.5, 3);
  }
  qa_q1mp_ext_make_static(c->game, e);
}
static void light_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rm_ctx *c = vctx; char path[64];
  const char *model = qa_q1mp_streq(e->classname, "light_lantern") ? "lantern" : "candle";
  (void)snprintf(path, sizeof path, "progs/%s.mdl", model);
  qa_q1mp_set_model(e, g->precache_model(g, path)); e->solid = "none"; e->movement = "none";
  qa_q1mp_ext_make_static(c->game, e);
}
static void rubble_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)vctx; (void)hn; (void)n; (void)s;
  qa_q1mp_actor *oe = g->entity(g, QA_Q1MP_SOME(other));
  if ((g->is_player(g, QA_Q1MP_SOME(other)) || (oe && (oe->movement_flags & 32) != 0)) && qa_q1mp_length(g->body(g, e).velocity) > 0)
    qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(e->actor.id), 10);
}
static void rubble_throw(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rm_ctx *c = vctx; qa_q1mp_game *game = c->game;
  qa_q1mp_actor *destination = qa_q1mp_find0(game, e->target);
  if (!destination) destination = g->world;
  if (!destination) qa_range_error("Rubble generator requires worldspawn");
  qa_vec3 direction = qa_q1mp_normalize(qa_q1mp_vsub(g->body(g, destination).origin, g->body(g, e).origin));
  qa_q1mp_actor *rubble = g->create(g, "rubble");
  rubble->owner = QA_Q1MP_SOME(e->actor.id); qa_q1mp_set_model(rubble, "progs/rubble.mdl");
  rubble->solid = "bbox"; rubble->movement = "bounce"; rubble->skin = (e->spawnflags & 1) != 0 ? 1 : 0;
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = g->body(g, e).origin;
  p.has_velocity = true; p.velocity = qa_q1mp_vscale(QA_Q1MP_V3(direction.x + qa_q1mp_random(game)*0.2-0.1, direction.y + qa_q1mp_random(game)*0.2-0.1, direction.z + qa_q1mp_random(game)*0.2-0.1), 300);
  p.has_bounds = true; p.bounds = (qa_bounds){{-16,-16,-16},{16,16,16}}; qa_q1mp_patch(game, rubble, &p);
  rubble->touch = g->named.touch(g->named.ctx, rubble, "rogue:rubble_touch");
  qa_q1mp_world_later(game, rubble, 30, "SUB_Remove"); g->link(g, rubble);
  qa_q1mp_world_later(game, e, e->delay, "rogue:rubble_throw");
}
static void rubble_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; (void)a; rm_ctx *c = vctx;
  if (e->wait == 0) { e->wait = 1; qa_q1mp_world_later(c->game, e, e->delay, "rogue:rubble_throw"); return; }
  e->wait = 0; g->cancel(g, e);
}
static void rubble_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  if (e->target[0] == '\0') qa_range_error("rubble_generator has no target!");
  if (e->delay == 0) e->delay = 5;
  e->solid = "none"; e->use = g->named.use(g->named.ctx, e, "rogue:rubble_use");
  if ((e->spawnflags & 2) != 0) qa_q1b_use_call(&e->use, QA_Q1MP_NONE, QA_Q1MP_NONE);
}
static void explosion_die(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor attacker) {
  rm_ctx *c = vctx; g->use_targets(g, e, attacker); e->touch.has = false;
  qa_q1mp_world_later(c->game, e, 0.1, "SUB_Remove");
}
static void explosion_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  rm_ctx *c = vctx; qa_q1mp_world_trigger(c->game, e);
  double health = g->health(g, e->actor.id); e->max_health = health != 0 ? health : 20;
  g->host->combat_set_health(g->host->ctx, e->actor, e->max_health);
  e->die = g->named.die(g->named.ctx, e, "rogue:explosion_trigger_die");
  qa_q1b_actor_set_damageable(g, e, true); e->solid = "bbox"; g->link(g, e);
}
void qa_q1mp_register_rogue_misc(qa_q1mp_game *game) {
  rm_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  if (game->replace_spawn) game->replace_spawn(game->ctx, "light_torch_small_walltorch", torch_spawn, c);
  else g->register_spawn(g, "light_torch_small_walltorch", torch_spawn, c);
  g->register_spawn(g, "light_lantern", light_spawn, c);
  g->register_spawn(g, "light_candle", light_spawn, c);
  qa_q1b_handlers h = {0}; h.ctx = c; h.touch = rubble_touch;
  g->named.register_named(g->named.ctx, "rogue:rubble_touch", &h);
  qa_q1b_handlers h2 = {0}; h2.ctx = c; h2.action = rubble_throw;
  g->named.register_named(g->named.ctx, "rogue:rubble_throw", &h2);
  qa_q1b_handlers h3 = {0}; h3.ctx = c; h3.use = rubble_use;
  g->named.register_named(g->named.ctx, "rogue:rubble_use", &h3);
  g->register_spawn(g, "rubble_generator", rubble_spawn, c);
  qa_q1b_handlers h4 = {0}; h4.ctx = c; h4.die = explosion_die;
  g->named.register_named(g->named.ctx, "rogue:explosion_trigger_die", &h4);
  g->register_spawn(g, "trigger_explosion", explosion_spawn, c);
  if (game->register_damage_source_effects) game->register_damage_source_effects(game->ctx, "rogue:explosion-trigger", c);
}
