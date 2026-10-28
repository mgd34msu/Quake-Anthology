/* Port of src/content/q3/base/game/mover-spawn.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/mover_spawn.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
static float f32(double v) { return (float)v; }
#define FRAMETIME_MS 100
#define CONTENTS_TRIGGER_V (double)(int32_t)0x40000000
static double float_int(double v) { return v >= -2147483648.0 && v < 2147483648.0 ? trunc(v) : -2147483648.0; }
static double comp(qa_vec3 v, int axis) {
  if (axis == 0) return v.x;
  if (axis == 1) return v.y;
  if (axis == 2) return v.z;
  qa_range_error("Door trigger axis is not a source vector index");
}
static qa_vec3 with_comp(qa_vec3 v, int axis, double amount) {
  if (axis == 0) return (qa_vec3){amount, v.y, v.z};
  if (axis == 1) return (qa_vec3){v.x, amount, v.z};
  if (axis == 2) return (qa_vec3){v.x, v.y, amount};
  qa_range_error("Door trigger axis is not a source vector index");
}
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb; }
typedef qa_q3_mover_spawn_runtime RT;
static double now(RT *r) { return r->host.time(r->host.ctx); }
static void owned(RT *r, qa_q3_game_entity *e) {
  if (qa_q3_entity_pool_get(r->host.pool, e->slot) != e) qa_range_error("Mover spawn entity belongs to another pool");
}
static qa_q3_game_entity *parent_of(qa_q3_game_entity *e) {
  if (!e->parent) qa_range_error("Mover trigger has no parent");
  return e->parent;
}
static void set_direction(qa_q3_game_entity *e) {
  qa_q3_move_direction_result m = qa_q3_move_direction(e->s.angles); e->s.angles = m.angles; e->movedir = m.direction;
}
static const char *vtos(RT *r, qa_vec3 v) { return (const char *)qa_q3_game_utility_scratch_vtos(r->host.pool->utilities, v); }
static void door_touch(void *ctx, qa_q3_game_entity *e, qa_q3_damage_participant other, qa_touch_contact contact) {
  (void)contact; RT *r = ctx; qa_q3_game_entity *parent = parent_of(e);
  if (other.entity && other.entity->client && other.entity->client->sess.session_team == 3) {
    if ((int)parent->mover_state == QA_Q3_MOVER_ONE_TO_TWO || (int)parent->mover_state == QA_Q3_MOVER_POS2) return;
    int axis = (int)e->count;
    double mn = comp(e->r.absmin, axis), mx = comp(e->r.absmax, axis), pos = comp(other.entity->s.origin, axis);
    bool toward_min = fabs(f32(pos - mx)) < fabs(f32(pos - mn));
    qa_vec3 dir = with_comp((qa_vec3){0, 0, 0}, axis, toward_min ? -1 : 1);
    qa_vec3 center = qa_scale3(qa_add3(e->r.absmin, e->r.absmax), 0.5);
    qa_vec3 origin = with_comp(center, axis, toward_min ? f32(mn - 10) : f32(mx + 10));
    qa_q3_teleport_player(r->host.teleport, other.entity, origin, qa_vector_to_angles(dir));
  } else if ((int)parent->mover_state != QA_Q3_MOVER_ONE_TO_TWO) r->host.use_binary(r->host.ctx, parent, e, other);
}
static void spawn_door_trigger(RT *r, qa_q3_game_entity *e) {
  for (qa_q3_game_entity *p = e; p; p = p->teamchain) qa_q3_game_entity_set_takedamage(p, true);
  qa_bounds b = {e->r.absmin, e->r.absmax};
  for (qa_q3_game_entity *p = e->teamchain; p; p = p->teamchain)
    b = qa_add_point_to_bounds(qa_add_point_to_bounds(b, p->r.absmin), p->r.absmax);
  qa_vec3 size = qa_sub3(b.max, b.min); int axis = 0;
  for (int i = 1; i < 3; i++) if (comp(size, i) < comp(size, axis)) axis = i;
  qa_q3_game_entity *t = qa_q3_entity_pool_spawn(r->host.pool);
  qa_q3_game_entity_set_classname(t, "door_trigger");
  t->r.mins = with_comp(b.min, axis, comp(b.min, axis) - 120);
  t->r.maxs = with_comp(b.max, axis, comp(b.max, axis) + 120);
  t->parent = e; t->r.contents = CONTENTS_TRIGGER_V; t->count = axis;
  t->touch = qa_q3_callback_family_resolve(&r->host.pool->callbacks.touch, "q3.base.game.mover-spawn.spawnDoorTrigger.touch");
  r->host.world.link(r->host.world.ctx, t);
  r->host.match_team(r->host.ctx, e, e->mover_state, now(r));
}
static void spawn_plat_trigger(RT *r, qa_q3_game_entity *e) {
  qa_q3_game_entity *t = qa_q3_entity_pool_spawn(r->host.pool);
  qa_q3_game_entity_set_classname(t, "plat_trigger");
  t->touch = qa_q3_callback_family_resolve(&r->host.pool->callbacks.touch, "q3.base.game.mover-spawn.spawnPlatTrigger.touch");
  t->r.contents = CONTENTS_TRIGGER_V; t->parent = e;
  qa_vec3 mn = qa_add3(qa_add3(e->pos1, e->r.mins), (qa_vec3){33, 33, 0});
  qa_vec3 mx = qa_add3(qa_add3(e->pos1, e->r.maxs), (qa_vec3){-33, -33, 8});
  for (int axis = 0; axis < 2; axis++) {
    if (comp(mx, axis) > comp(mn, axis)) continue;
    double center = f32(comp(e->pos1, axis) + f32(f32(comp(e->r.mins, axis) + comp(e->r.maxs, axis)) * 0.5));
    mn = with_comp(mn, axis, center); mx = with_comp(mx, axis, center + 1);
  }
  t->r.mins = mn; t->r.maxs = mx; r->host.world.link(r->host.world.ctx, t);
}
static void reached_train(RT *r, qa_q3_game_entity *e) {
  qa_q3_game_entity *next = e->next_train;
  if (!next || !next->next_train) return;
  qa_q3_target_use_context uc; memset(&uc, 0, sizeof uc);
  uc.pool = r->host.pool; uc.time = now(r); uc.ctx = r->host.ctx; uc.remap_shader = r->host.remap_shader; uc.warn = r->host.warn;
  qa_q3_use_targets(uc, next, NULL);
  qa_q3_game_entity *dest = next->next_train;
  if (!dest) qa_range_error("Train path was removed during target dispatch");
  e->next_train = dest; e->pos1 = next->s.origin; e->pos2 = dest->s.origin;
  double speed = next->speed != 0 ? next->speed : e->speed; if (speed < 1) speed = 1;
  e->s.pos.duration = float_int(f32(f32(qa_length3(qa_sub3(e->pos2, e->pos1)) * 1000) / (float)speed));
  e->s.loop_sound = next->sound_loop;
  r->host.set_state(r->host.ctx, e, QA_Q3_MOVER_ONE_TO_TWO, now(r));
  if (next->wait != 0) {
    qa_q3_game_entity_set_nextthink(e, float_int(f32(f32(now(r)) + f32(next->wait * 1000))));
    e->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.base.game.mover-spawn.reachedTrain.think");
    e->s.pos.type = QA_Q3_TR_STATIONARY;
  }
}
static void setup_train(RT *r, qa_q3_game_entity *e) {
  char buf[512];
  qa_q3_game_entity *start = qa_q3_find_entity(r->host.pool, NULL, QA_Q3_FIELD_TARGETNAME, e->target);
  e->next_train = start;
  if (!start) { snprintf(buf, sizeof buf, "func_train at %s with an unfound target\n", vtos(r, e->r.absmin)); r->host.warn(r->host.ctx, buf); return; }
  qa_q3_game_entity *visited[1024]; size_t nv = 0; qa_q3_game_entity *path = start;
  for (;;) {
    for (size_t i = 0; i < nv; i++) if (visited[i] == path) qa_range_error("Train path cycle does not return to its first corner");
    if (nv < 1024) visited[nv++] = path;
    if (!path->target) { snprintf(buf, sizeof buf, "Train corner at %s without a target\n", vtos(r, path->s.origin)); r->host.warn(r->host.ctx, buf); return; }
    qa_q3_game_entity *next = NULL;
    for (;;) {
      next = qa_q3_find_entity(r->host.pool, next, QA_Q3_FIELD_TARGETNAME, path->target);
      if (!next) { snprintf(buf, sizeof buf, "Train corner at %s without a target path_corner\n", vtos(r, path->s.origin)); r->host.warn(r->host.ctx, buf); return; }
      const char *cn = qa_q3_game_entity_classname(next);
      if (cn && strcmp(cn, "path_corner") == 0) break;
    }
    path->next_train = next; path = next;
    if (path == start) break;
  }
  reached_train(r, e);
}
/* Spawn bodies (each wrapped by owned() in its handler). */
static void spawn_door(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  e->sound_1to2 = e->sound_2to1 = r->host.sound_index(r->host.ctx, "sound/movers/doors/dr1_strt.wav");
  e->sound_pos1 = e->sound_pos2 = r->host.sound_index(r->host.ctx, "sound/movers/doors/dr1_end.wav");
  e->blocked = qa_q3_callback_family_resolve(&r->host.pool->callbacks.blocked, "q3.base.game.mover-spawn.door.blocked");
  if (e->speed == 0) e->speed = 400;
  if (e->wait == 0) e->wait = 2;
  e->wait = f32(e->wait * 1000);
  double lip = qa_q3_spawn_variables_float(v, "lip", "8").value; e->damage = qa_q3_spawn_variables_int(v, "dmg", "2").value;
  e->pos1 = e->s.origin; r->host.set_brush_model(r->host.ctx, e, e->model); set_direction(e);
  qa_vec3 absd = {fabs(e->movedir.x), fabs(e->movedir.y), fabs(e->movedir.z)};
  double dist = f32(qa_dot3(absd, qa_sub3(e->r.maxs, e->r.mins)) - lip);
  e->pos2 = qa_add3(e->pos1, qa_scale3(e->movedir, dist));
  if (((int)e->spawnflags & 1) != 0) { e->pos1 = e->pos2; e->pos2 = e->s.origin; }
  r->host.initialize_binary(r->host.ctx, e, v);
  qa_q3_game_entity_set_nextthink(e, qa_int32(now(r) + FRAMETIME_MS));
  if (((int)e->flags & QA_Q3_GAME_FLAG_TEAMSLAVE) == 0) {
    double health = qa_q3_spawn_variables_int(v, "health", "0").value;
    if (health != 0) qa_q3_game_entity_set_takedamage(e, true);
    e->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think,
        e->targetname || health != 0 ? "q3.base.game.mover-spawn.door.matchTeam" : "q3.base.game.mover-spawn.door.spawnTrigger");
  }
}
static void spawn_plat(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  e->sound_1to2 = e->sound_2to1 = r->host.sound_index(r->host.ctx, "sound/movers/plats/pt1_strt.wav");
  e->sound_pos1 = e->sound_pos2 = r->host.sound_index(r->host.ctx, "sound/movers/plats/pt1_end.wav");
  e->s.angles = (qa_vec3){0, 0, 0};
  e->speed = qa_q3_spawn_variables_float(v, "speed", "200").value; e->damage = qa_q3_spawn_variables_int(v, "dmg", "2").value;
  e->wait = 1000; double lip = qa_q3_spawn_variables_float(v, "lip", "8").value;
  r->host.set_brush_model(r->host.ctx, e, e->model);
  qa_q3_spawn_value_number height = qa_q3_spawn_variables_float(v, "height", "0");
  double dist = height.present ? height.value : f32(f32(e->r.maxs.z - e->r.mins.z) - lip);
  e->pos2 = e->s.origin; e->pos1 = (qa_vec3){e->pos2.x, e->pos2.y, e->pos2.z - dist};
  r->host.initialize_binary(r->host.ctx, e, v);
  e->touch = qa_q3_callback_family_resolve(&r->host.pool->callbacks.touch, "q3.base.game.mover-spawn.plat.touch");
  e->blocked = qa_q3_callback_family_resolve(&r->host.pool->callbacks.blocked, "q3.base.game.mover-spawn.door.blocked");
  e->parent = e; if (!e->targetname) spawn_plat_trigger(r, e);
}
static void spawn_button(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  e->sound_1to2 = r->host.sound_index(r->host.ctx, "sound/movers/switches/butn2.wav");
  if (e->speed == 0) e->speed = 40;
  if (e->wait == 0) e->wait = 1;
  e->wait = f32(e->wait * 1000);
  e->pos1 = e->s.origin; r->host.set_brush_model(r->host.ctx, e, e->model); set_direction(e);
  qa_vec3 absd = {fabs(e->movedir.x), fabs(e->movedir.y), fabs(e->movedir.z)};
  double dist = f32(qa_dot3(absd, qa_sub3(e->r.maxs, e->r.mins)) - qa_q3_spawn_variables_float(v, "lip", "4").value);
  e->pos2 = qa_add3(e->pos1, qa_scale3(e->movedir, dist));
  if (qa_q3_game_entity_health(e) != 0) qa_q3_game_entity_set_takedamage(e, true);
  else e->touch = qa_q3_callback_family_resolve(&r->host.pool->callbacks.touch, "q3.base.game.mover-spawn.button.touch");
  r->host.initialize_binary(r->host.ctx, e, v);
}
static void spawn_train(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  char buf[512]; e->s.angles = (qa_vec3){0, 0, 0};
  if (((int)e->spawnflags & 4) != 0) e->damage = 0; else if (e->damage == 0) e->damage = 2;
  if (e->speed == 0) e->speed = 100;
  if (!e->target) { snprintf(buf, sizeof buf, "func_train without a target at %s\n", vtos(r, e->r.absmin)); r->host.warn(r->host.ctx, buf); qa_q3_entity_pool_free(r->host.pool, e); return; }
  r->host.set_brush_model(r->host.ctx, e, e->model); r->host.initialize_binary(r->host.ctx, e, v);
  e->reached = qa_q3_callback_family_resolve(&r->host.pool->callbacks.reached, "q3.base.game.mover-spawn.train.reached");
  qa_q3_game_entity_set_nextthink(e, qa_int32(now(r) + FRAMETIME_MS));
  e->think = qa_q3_callback_family_resolve(&r->host.pool->callbacks.think, "q3.base.game.mover-spawn.train.think");
}
static void spawn_path_corner(RT *r, qa_q3_game_entity *e) {
  char buf[512]; if (e->targetname) return;
  snprintf(buf, sizeof buf, "path_corner with no targetname at %s\n", vtos(r, e->s.origin));
  r->host.warn(r->host.ctx, buf); qa_q3_entity_pool_free(r->host.pool, e);
}
static void spawn_static(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  r->host.set_brush_model(r->host.ctx, e, e->model); r->host.initialize_binary(r->host.ctx, e, v);
  e->s.pos.base = e->s.origin; e->r.current_origin = e->s.origin;
}
static void spawn_rotating(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  if (e->speed == 0) e->speed = 100;
  int axis = ((int)e->spawnflags & 4) != 0 ? 2 : ((int)e->spawnflags & 8) != 0 ? 0 : 1;
  e->s.apos.type = QA_Q3_TR_LINEAR; e->s.apos.delta = with_comp(e->s.apos.delta, axis, e->speed);
  if (e->damage == 0) e->damage = 2;
  r->host.set_brush_model(r->host.ctx, e, e->model); r->host.initialize_binary(r->host.ctx, e, v);
  e->s.pos.base = e->s.origin; e->r.current_origin = e->s.pos.base; e->r.current_angles = e->s.apos.base;
  r->host.world.link(r->host.world.ctx, e);
}
static void spawn_bobbing(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  e->speed = qa_q3_spawn_variables_float(v, "speed", "4").value; e->damage = qa_q3_spawn_variables_int(v, "dmg", "2").value;
  double height = qa_q3_spawn_variables_float(v, "height", "32").value, phase = qa_q3_spawn_variables_float(v, "phase", "0").value;
  r->host.set_brush_model(r->host.ctx, e, e->model); r->host.initialize_binary(r->host.ctx, e, v);
  e->r.current_origin = e->s.origin;
  double duration = float_int(f32(e->speed * 1000));
  int axis = ((int)e->spawnflags & 1) != 0 ? 0 : ((int)e->spawnflags & 2) != 0 ? 1 : 2;
  e->s.pos.base = e->s.origin; e->s.pos.duration = duration; e->s.pos.time = float_int(f32(f32(duration) * (float)phase));
  e->s.pos.type = QA_Q3_TR_SINE; e->s.pos.delta = with_comp(e->s.pos.delta, axis, height);
}
static void spawn_pendulum(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  double speed = qa_q3_spawn_variables_float(v, "speed", "30").value, phase = qa_q3_spawn_variables_float(v, "phase", "0").value;
  e->damage = qa_q3_spawn_variables_int(v, "dmg", "2").value; r->host.set_brush_model(r->host.ctx, e, e->model);
  double length = fmax(8, fabs(e->r.mins.z));
  double freq = f32(f32(1 / f32(f32(3.141592653589793) * 2)) * f32(sqrt(f32(r->host.gravity(r->host.ctx) / f32(3 * length)))));
  double duration = float_int(f32(1000 / freq)); e->s.pos.duration = duration;
  r->host.initialize_binary(r->host.ctx, e, v);
  e->s.pos.base = e->s.origin; e->r.current_origin = e->s.origin;
  e->s.apos.base = e->s.angles; e->s.apos.duration = duration; e->s.apos.time = float_int(f32(f32(duration) * (float)phase));
  e->s.apos.type = QA_Q3_TR_SINE; e->s.apos.delta = with_comp(e->s.apos.delta, 2, speed);
}
/* Save callbacks. */
static void cb_blocked(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o) { RT *r = ctx; r->host.blocked_door(r->host.ctx, s, o); }
static void cb_match(void *ctx, qa_q3_game_entity *s) { RT *r = ctx; r->host.match_team(r->host.ctx, s, s->mover_state, now(r)); }
static void cb_door_trig(void *ctx, qa_q3_game_entity *s) { spawn_door_trigger(ctx, s); }
static void cb_plat_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) {
  (void)c; RT *r = ctx; qa_q3_game_entity *p = parent_of(s);
  if (o.entity && o.entity->client && (int)p->mover_state == QA_Q3_MOVER_POS1) r->host.use_binary(r->host.ctx, p, s, o);
}
static void cb_plat_self_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) {
  (void)c; RT *r = ctx;
  if (o.entity && o.entity->client && o.entity->client->ps.health > 0 && (int)s->mover_state == QA_Q3_MOVER_POS2)
    qa_q3_game_entity_set_nextthink(s, qa_int32(now(r) + 1000));
}
static void cb_button_touch(void *ctx, qa_q3_game_entity *s, qa_q3_damage_participant o, qa_touch_contact c) {
  (void)c; RT *r = ctx;
  if (o.entity && o.entity->client && (int)s->mover_state == QA_Q3_MOVER_POS1) r->host.use_binary(r->host.ctx, s, o.entity, o);
}
static void cb_reached_think(void *ctx, qa_q3_game_entity *s) { RT *r = ctx; s->s.pos.time = now(r); s->s.pos.type = QA_Q3_TR_LINEAR_STOP; }
static void cb_reached(void *ctx, qa_q3_game_entity *s) { reached_train(ctx, s); }
static void cb_train_think(void *ctx, qa_q3_game_entity *s) { setup_train(ctx, s); }
void qa_q3_mover_spawn_bind_save_callbacks(RT *r) {
  qa_q3_callback_catalog *c = &r->host.pool->callbacks;
  qa_q3_entity_touch_fn dt = door_touch, pt = cb_plat_touch, pst = cb_plat_self_touch, bt = cb_button_touch;
  qa_q3_entity_blocked_fn bl = cb_blocked;
  qa_q3_entity_think_fn mt = cb_match, dtt = cb_door_trig, rt2 = cb_reached_think, tt = cb_train_think, rc = cb_reached;
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.mover-spawn.spawnDoorTrigger.touch", mkcb(&dt, r));
  qa_q3_callback_family_intern(&c->blocked, "q3.base.game.mover-spawn.door.blocked", mkcb(&bl, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.mover-spawn.door.matchTeam", mkcb(&mt, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.mover-spawn.door.spawnTrigger", mkcb(&dtt, r));
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.mover-spawn.spawnPlatTrigger.touch", mkcb(&pt, r));
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.mover-spawn.plat.touch", mkcb(&pst, r));
  qa_q3_callback_family_intern(&c->touch, "q3.base.game.mover-spawn.button.touch", mkcb(&bt, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.mover-spawn.reachedTrain.think", mkcb(&rt2, r));
  qa_q3_callback_family_intern(&c->reached, "q3.base.game.mover-spawn.train.reached", mkcb(&rc, r));
  qa_q3_callback_family_intern(&c->think, "q3.base.game.mover-spawn.train.think", mkcb(&tt, r));
}
bool qa_q3_mover_spawn_is_door_trigger(RT *r, qa_q3_game_entity *e) {
  qa_q3_entity_callback want = qa_q3_callback_family_resolve(&r->host.pool->callbacks.touch, "q3.base.game.mover-spawn.spawnDoorTrigger.touch");
  return e->touch.fn == want.fn && e->touch.ctx == want.ctx;
}
void qa_q3_mover_spawn_runtime_init(RT *r, qa_q3_mover_spawn_host host) { r->host = host; qa_q3_mover_spawn_bind_save_callbacks(r); }
static void h_door(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { owned(c, e); spawn_door(c, e, v); }
static void h_plat(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { owned(c, e); spawn_plat(c, e, v); }
static void h_button(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { owned(c, e); spawn_button(c, e, v); }
static void h_train(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { owned(c, e); spawn_train(c, e, v); }
static void h_corner(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { (void)v; owned(c, e); spawn_path_corner(c, e); }
static void h_static(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { owned(c, e); spawn_static(c, e, v); }
static void h_rot(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { owned(c, e); spawn_rotating(c, e, v); }
static void h_bob(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { owned(c, e); spawn_bobbing(c, e, v); }
static void h_pend(void *c, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) { owned(c, e); spawn_pendulum(c, e, v); }
size_t qa_q3_mover_spawn_runtime_handlers(RT *r, qa_q3_spawn_handler_entry *out, size_t cap) {
  static const struct { const char *name; qa_q3_spawn_handler_fn fn; } T[] = {
    {"func_door", h_door}, {"func_plat", h_plat}, {"func_button", h_button}, {"func_train", h_train},
    {"path_corner", h_corner}, {"func_static", h_static}, {"func_rotating", h_rot},
    {"func_bobbing", h_bob}, {"func_pendulum", h_pend}};
  size_t n = sizeof T / sizeof *T;
  for (size_t i = 0; i < n && i < cap; i++) { out[i].classname = T[i].name; out[i].fn = T[i].fn; out[i].ctx = r; }
  return n;
}
size_t qa_q3_create_mover_spawn_handlers(qa_q3_mover_spawn_host host, qa_q3_spawn_handler_entry *out, size_t cap) {
  RT *r = malloc(sizeof *r); if (!r) qa_range_error("out of memory");
  qa_q3_mover_spawn_runtime_init(r, host); return qa_q3_mover_spawn_runtime_handlers(r, out, cap);
}
