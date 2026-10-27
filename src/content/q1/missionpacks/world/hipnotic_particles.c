/* Port of src/content/q1/missionpacks/world/hipnotic-particles.ts.
 * makeStatic via rogue_misc ext declaration pattern. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_particles.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include <stdlib.h>

void qa_q1mp_ext_make_static(qa_q1mp_game *game, qa_q1mp_actor *entity);
typedef struct hp_ctx { qa_q1mp_game *game; } hp_ctx;
static void field_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor other, qa_q1mp_opt_actor a) {
  (void)a; hp_ctx *c = vctx; qa_q1mp_game *game = c->game;
  qa_q1mp_actor *counter = other.has ? g->entity(g, other) : NULL;
  if ((e->spawnflags & 1) != 0 && (counter && qa_q1mp_streq(counter->classname,"func_counter") ? qa_q1mp_number(counter,"counter_state") : 0) != qa_q1mp_number(e,"cnt")) return;
  qa_q1mp_world_number(e, "ltime", g->time + 0.25);
  const char *noise = qa_q1mp_text(e, "noise");
  if (noise[0]) qa_q1mp_sound_ch(game, e, noise, "voice");
  qa_q1mp_opt_actor client = g->host->check_client(g->host->ctx, e->actor);
  if (!client.has) return;
  qa_vec3 start = qa_q1mp_vadd(e->dest1, g->body(g, e).origin), end = qa_q1mp_vadd(e->dest2, g->body(g, e).origin);
  double plane = qa_q1mp_number(e, "particle_plane"), color = qa_q1mp_number(e, "color");
  if (plane == 0) { for (double x=start.x;x<=end.x;x+=16) for (double z=start.z;z<=end.z;z+=16) if (game->emit_particles) game->emit_particles(game->ctx, QA_Q1MP_V3(x,start.y,z), QA_Q1MP_ZERO, color, e->count); }
  else if (plane == 1) { for (double y=start.y;y<end.y;y+=16) for (double z=start.z;z<end.z;z+=16) if (game->emit_particles) game->emit_particles(game->ctx, QA_Q1MP_V3(start.x,y,z), QA_Q1MP_ZERO, color, e->count); }
  else { for (double x=start.x;x<end.x;x+=16) for (double y=start.y;y<end.y;y+=16) if (game->emit_particles) game->emit_particles(game->ctx, QA_Q1MP_V3(x,y,start.z), QA_Q1MP_ZERO, color, e->count); }
}
static void field_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)vctx; (void)hn; (void)n; (void)s;
  if (e->damage == 0 || g->time > qa_q1mp_number(e,"ltime") || g->time < e->attack_finished) return;
  e->attack_finished = g->time + 0.5; qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(e->actor.id), e->damage);
}
static void particlefield_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx; qa_bounds bounds = g->body(g, e).bounds;
  qa_vec3 origin = qa_q1mp_vscale(qa_q1mp_vadd(bounds.min, bounds.max), 0.5);
  qa_vec3 size = qa_q1mp_vsub(qa_q1mp_vsub(bounds.max, bounds.min), QA_Q1MP_V3(16,16,16));
  e->dest1 = qa_q1mp_vsub(qa_q1mp_vadd(bounds.min, QA_Q1MP_V3(8,8,8)), origin);
  e->dest2 = qa_q1mp_vsub(qa_q1mp_vadd(bounds.max, QA_Q1MP_V3(7.9,7.9,7.9)), origin);
  if (size.x > size.z && size.y > size.z) { qa_q1mp_world_number(e, "particle_plane", 2); e->dest1.z = (e->dest1.z + e->dest2.z) / 2; }
  else if (size.x <= size.z && size.y > size.x) { qa_q1mp_world_number(e, "particle_plane", 1); e->dest1.x = (e->dest1.x + e->dest2.x) / 2; }
  else { qa_q1mp_world_number(e, "particle_plane", 0); e->dest1.y = (e->dest1.y + e->dest2.y) / 2; }
  e->model[0] = '\0'; e->solid = "none"; e->movement = "none";
  if (e->count == 0) e->count = 2;
  if (qa_q1mp_number(e, "color") == 0) qa_q1mp_world_number(e, "color", 192);
  e->use = g->named.use(g->named.ctx, e, "hip:particlefield");
  e->touch = g->named.touch(g->named.ctx, e, "hip:particlefield");
  g->set_origin(g, e, origin);
}
static void togglewall_use(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)vctx; (void)o; (void)a;
  bool active = qa_q1mp_number(e, "toggle_state") == 0;
  qa_q1mp_world_number(e, "toggle_state", active ? 1 : 0);
  double d = active ? -8000 : 8000;
  g->set_origin(g, e, qa_q1mp_vadd(g->body(g, e).origin, QA_Q1MP_V3(d,d,d)));
  g->sound(g, e, qa_q1mp_text(e, active ? "noise1" : "noise"), QA_Q1MP_CH("voice"), 1, 1);
}
static void togglewall_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)vctx; (void)hn; (void)n; (void)s;
  if (e->damage == 0 || g->time < e->attack_finished) return;
  e->attack_finished = g->time + 0.5; qa_q1b_game_damage(g, other, e->actor.id, QA_Q1MP_SOME(e->actor.id), e->damage);
}
static void togglewall_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  e->movement = "push"; e->solid = "bsp"; e->model[0] = '\0';
  e->use = g->named.use(g->named.ctx, e, "hip:togglewall");
  e->touch = g->named.touch(g->named.ctx, e, "hip:togglewall");
  if (!qa_q1mp_text(e, "noise")[0]) qa_q1b_fields_set(e, "noise", "misc/null.wav");
  if (!qa_q1mp_text(e, "noise1")[0]) qa_q1b_fields_set(e, "noise1", "misc/null.wav");
  if ((e->spawnflags & 1) != 0) { qa_q1mp_world_number(e, "toggle_state", 0); g->set_origin(g, e, qa_q1mp_vadd(g->body(g, e).origin, QA_Q1MP_V3(8000,8000,8000))); return; }
  qa_q1mp_world_number(e, "toggle_state", 1);
  g->sound(g, e, qa_q1mp_text(e, "noise1"), QA_Q1MP_CH("voice"), 1, 1);
}
static void wallsprite_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  hp_ctx *c = vctx;
  if (!e->model[0]) qa_q1mp_set_model(e, "progs/s_blood1.spr");
  (void)g->precache_model(g, e->model); e->solid = "none"; e->movement = "none";
  qa_vec3 angles = g->body(g, e).angles;
  if (angles.x == 0 && angles.y == -1 && angles.z == 0) angles = QA_Q1MP_V3(-90,0,0);
  else if (angles.x == 0 && angles.y == -2 && angles.z == 0) angles = QA_Q1MP_V3(90,0,0);
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = angles;
  p.has_origin = true; p.origin = qa_q1mp_vsub(g->body(g, e).origin, qa_q1mp_vscale(qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_Q1).forward, 0.2));
  qa_q1mp_patch(c->game, e, &p);
  qa_q1mp_ext_make_static(c->game, e);
}
void qa_q1mp_register_hipnotic_particles(qa_q1mp_game *game) {
  hp_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.use = field_use; h.touch = field_touch;
  g->named.register_named(g->named.ctx, "hip:particlefield", &h);
  g->register_spawn(g, "func_particlefield", particlefield_spawn, c);
  qa_q1b_handlers ht = {0}; ht.ctx = c; ht.use = togglewall_use; ht.touch = togglewall_touch;
  g->named.register_named(g->named.ctx, "hip:togglewall", &ht);
  g->register_spawn(g, "func_togglewall", togglewall_spawn, c);
  g->register_spawn(g, "wallsprite", wallsprite_spawn, c);
}
