/* Port of src/content/q1/missionpacks/world/hipnotic-train.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_train.h"
#include "../missionpacks_internal.h"
#include "qa/content/q1/missionpacks/world/common.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdlib.h>

typedef struct ht_ctx { qa_q1mp_game *game; } ht_ctx;
static void train_next(ht_ctx *c, qa_q1b_game *g, qa_q1b_actor *e) {
  double current = qa_q1mp_number(e, "cnt");
  qa_q1mp_actor *corner = qa_q1mp_find0(c->game, e->target);
  if (!corner) qa_range_error("hip_train_next: missing %s", e->target);
  qa_q1mp_world_number(e, "cnt", corner->speed);
  qa_q1mp_set_target(e, corner->target);
  if (e->target[0] == '\0') qa_range_error("hip_train_next: no next target");
  qa_q1mp_sound(c->game, e, qa_q1mp_text(e, "noise1")); e->wait = corner->wait;
  qa_q1mp_opt_actor prior_ref = qa_q1b_references_get(e, "goalentity");
  qa_q1mp_actor *prior = prior_ref.has ? g->entity(g, prior_ref) : NULL;
  if (prior && qa_q1mp_text(prior, "event")[0]) qa_q1mp_world_target_event(c->game, e, qa_q1mp_text(prior, "event"), prior->message);
  qa_q1b_references_set(e, "goalentity", QA_Q1MP_SOME(corner->actor.id));
  const char *next = corner->wait != 0 ? "hip:train_wait" : "hip:train_next";
  qa_vec3 destination = qa_q1mp_vsub(g->body(g, corner).origin, g->body(g, e).bounds.min);
  if (current == -1) {
    g->set_origin(g, e, destination);
    if (g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + 0.01), g->named.action(g->named.ctx, e, next));
    return;
  }
  if (current > 0) e->speed = current;
  if (g->calc_move) g->calc_move(g, e, destination, e->speed, g->named.action(g->named.ctx, e, next));
}
static void train_next_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) { train_next(vctx, g, e); }
static void train_find_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ht_ctx *c = vctx; qa_q1mp_actor *corner = qa_q1mp_find0(c->game, e->target);
  if (!corner) qa_range_error("hip_func_train_find: missing %s", e->target);
  qa_q1b_references_set(e, "goalentity", QA_Q1MP_SOME(corner->actor.id));
  qa_q1mp_world_number(e, "cnt", corner->speed); qa_q1mp_set_target(e, corner->target);
  g->set_origin(g, e, qa_q1mp_vsub(g->body(g, corner).origin, g->body(g, e).bounds.min));
  if (e->targetname[0] == '\0' && g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + 0.1), g->named.action(g->named.ctx, e, "hip:train_next"));
}
static void train_wait_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  (void)vctx;
  if (e->wait != 0) {
    g->sound(g, e, qa_q1mp_text(e, "noise"), QA_Q1MP_CH("auto"), 1, 1);
    if (e->wait == -1) return;
    double wait = e->wait; e->wait = 0;
    if (g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + wait), g->named.action(g->named.ctx, e, "hip:train_next"));
    return;
  }
  if (g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + 0.1), g->named.action(g->named.ctx, e, "hip:train_next"));
}
static void train_use_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_q1mp_opt_actor o, qa_q1mp_opt_actor a) {
  (void)o; qa_vec3 v = g->body(g, e).velocity;
  if (v.x != 0 || v.y != 0 || v.z != 0) return;
  e->activator = a; train_next(vctx, g, e);
}
static void train_blocked_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other) {
  (void)vctx;
  if (e->attack_finished > g->time) return;
  e->attack_finished = g->time + 0.5;
  (void)g->damage(g, other, e->actor.id, QA_Q1MP_SOME(e->actor.id), e->damage, NULL, "direct", "crush", NULL);
}
static void train_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ht_ctx *c = vctx;
  if (e->target[0] == '\0') qa_range_error("func_train2 without a target");
  if (e->speed == 0) e->speed = 100;
  if (e->damage == 0) e->damage = 2;
  qa_q1mp_world_brush(c->game, e); qa_q1mp_world_number(e, "cnt", 1);
  if (!qa_q1mp_text(e, "noise")[0]) qa_q1b_fields_set(e, "noise", e->sounds == 1 ? "plats/train2.wav" : "misc/null.wav");
  if (!qa_q1mp_text(e, "noise1")[0]) qa_q1b_fields_set(e, "noise1", e->sounds == 1 ? "plats/train1.wav" : "misc/null.wav");
  e->use = g->named.use(g->named.ctx, e, "hip:train_use");
  e->blocked = g->named.blocked(g->named.ctx, e, "hip:train_blocked");
  if (g->schedule_at) g->schedule_at(g, e, qa_q1mp_fround(qa_q1mp_number(e,"ltime") + 0.1), g->named.action(g->named.ctx, e, "hip:train_find"));
}
static void bobbing_cb(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ht_ctx *c = vctx;
  e->count = qa_q1mp_fround(e->count + e->speed * (g->time - qa_q1mp_number(e, "ltime")));
  if (e->count > 360) e->count -= 360;
  qa_vec3 origin = g->body(g, e).origin;
  origin.z = qa_q1mp_fround(qa_math_angle_vectors_rule(QA_Q1MP_V3(e->count,0,0), QA_ANGLE_RULE_Q1).forward.z * qa_q1mp_number(e, "cnt"));
  g->set_origin(g, e, origin); qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1mp_world_later(c->game, e, 0.02, "hip:bobbing_water");
}
static void bobbing_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ht_ctx *c = vctx;
  e->solid = "none"; e->movement = "step"; e->count = 0; e->speed = 360 / (e->speed != 0 ? e->speed : 4);
  qa_bounds b = g->body(g, e).bounds;
  qa_q1mp_world_number(e, "cnt", (b.max.z - b.min.z) / 2); qa_q1mp_world_number(e, "ltime", g->time);
  qa_q1b_body_patch p = {0}; p.has_angles = true; p.angles = QA_Q1MP_ZERO; g->set_body(g, e, &p);
  qa_q1mp_world_later(c->game, e, 0.02, "hip:bobbing_water");
}
static void pushable_touch(void *vctx, qa_q1b_game *g, qa_q1b_actor *e, qa_actor_id other, bool hn, qa_vec3 n, const void *s) {
  (void)hn; (void)n; (void)s; ht_ctx *c = vctx;
  qa_q1b_body body; bool has = qa_q1mp_body_read(c->game, other, &body);
  qa_q1mp_actor *owner = g->entity(g, e->owner);
  if (!has || !owner) return;
  qa_vec3 velocity = body.velocity;
  double yaw = fabs(velocity.x) > fabs(velocity.y) ? (velocity.x > 0 ? 0 : 180) : (velocity.y > 0 ? 90 : 270);
  if (g->host->walk_move) (void)g->host->walk_move(g->host->ctx, e->actor, yaw, 16 * g->frame_seconds);
  g->set_origin(g, owner, qa_q1mp_vadd(qa_q1mp_vector_of(owner, "oldorigin"), qa_q1mp_vsub(g->body(g, e).origin, qa_q1mp_vector_of(e, "oldorigin"))));
}
static void pushable_spawn(void *vctx, qa_q1b_game *g, qa_q1b_actor *e) {
  ht_ctx *c = vctx; qa_q1mp_world_brush(c->game, e);
  qa_q1b_body body = g->body(g, e); qa_q1mp_world_vector(e, "oldorigin", body.origin);
  qa_q1mp_actor *proxy = g->create(g, "pushablewallproxy");
  proxy->owner = QA_Q1MP_SOME(e->actor.id); proxy->solid = "bbox"; proxy->movement = "step";
  qa_vec3 size = qa_q1mp_vscale(qa_q1mp_vsub(body.bounds.max, body.bounds.min), 0.5);
  qa_vec3 origin = qa_q1mp_vadd(qa_q1mp_vscale(qa_q1mp_vadd(body.bounds.min, body.bounds.max), 0.5), QA_Q1MP_V3(0,0,1));
  qa_q1mp_world_vector(proxy, "oldorigin", origin);
  qa_q1b_body_patch p = {0}; p.has_origin = true; p.origin = origin;
  p.has_bounds = true; p.bounds.min = qa_q1mp_vsub(QA_Q1MP_V3(-1,-1,0), size); p.bounds.max = qa_q1mp_vadd(QA_Q1MP_V3(1,1,-2), size);
  qa_q1mp_patch(c->game, proxy, &p);
  proxy->touch = g->named.touch(g->named.ctx, proxy, "hip:pushable_touch");
  g->link(g, proxy);
}
void qa_q1mp_register_hipnotic_train(qa_q1mp_game *game) {
  ht_ctx *c = malloc(sizeof *c); c->game = game; qa_q1b_game *g = game->base;
  qa_q1b_handlers h = {0}; h.ctx = c; h.action = train_next_cb;
  g->named.register_named(g->named.ctx, "hip:train_next", &h);
  qa_q1b_handlers hf = {0}; hf.ctx = c; hf.action = train_find_cb;
  g->named.register_named(g->named.ctx, "hip:train_find", &hf);
  qa_q1b_handlers hw2 = {0}; hw2.ctx = c; hw2.action = train_wait_cb;
  g->named.register_named(g->named.ctx, "hip:train_wait", &hw2);
  qa_q1b_handlers hu = {0}; hu.ctx = c; hu.use = train_use_cb;
  g->named.register_named(g->named.ctx, "hip:train_use", &hu);
  qa_q1b_handlers hb = {0}; hb.ctx = c; hb.blocked = train_blocked_cb;
  g->named.register_named(g->named.ctx, "hip:train_blocked", &hb);
  g->register_spawn(g, "func_train2", train_spawn, c);
  qa_q1b_handlers hbo = {0}; hbo.ctx = c; hbo.action = bobbing_cb;
  g->named.register_named(g->named.ctx, "hip:bobbing_water", &hbo);
  g->register_spawn(g, "func_bobbingwater", bobbing_spawn, c);
  qa_q1b_handlers hp = {0}; hp.ctx = c; hp.touch = pushable_touch;
  g->named.register_named(g->named.ctx, "hip:pushable_touch", &hp);
  g->register_spawn(g, "func_pushable", pushable_spawn, c);
}
