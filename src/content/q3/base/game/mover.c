/* Port of src/content/q3/base/game/mover.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/mover.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q3/base/game/ground.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/core/math.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/foundation/js.h"
enum { EF_MOVER_STOP = 0x400, MOD_CRUSH = 17, SVF_USE_CURRENT_ORIGIN = 128, ET_PLAYER = 1, ET_ITEM = 2, ET_MISSILE = 3, ET_MOVER = 4 };
typedef qa_q3_mover_runtime RT;
#define IMPL(name) "q3.base.game.mover." name
static qa_q3_game_combat_context *combat_of(RT *r) { return r->host.combat(r->host.ctx); }
static qa_q3_entity_pool *pool_of(RT *r) { return combat_of(r)->entities; }
static double now(RT *r) { return combat_of(r)->time; }
static void link_entity(RT *r, qa_q3_game_entity *e) { r->host.world.link(r->host.world.ctx, e); }
static void owned(RT *r, qa_q3_game_entity *e) { if (qa_q3_entity_pool_get(pool_of(r), e->slot) != e) qa_range_error("Mover entity does not belong to this pool"); }
static double f32(double v) { return (float)v; }
static void base_origin(qa_q3_game_entity *e, qa_vec3 origin) { e->s.pos.base = origin; }
static void base_angles(qa_q3_game_entity *e, qa_vec3 angles) { e->s.apos.base = angles; }
static double angle_short(double angle) { return qa_qvm_float_to_int(f32(f32(angle * 65536) / 360)) & 65535; }
static qa_vec3 push_rotation(qa_vec3 origin, qa_vec3 pusher_origin, qa_vec3 amove) {
  qa_angle_vectors axes = qa_qvm_angle_vectors(amove);
  qa_vec3 m0 = axes.forward, m1 = qa_scale3(axes.right, -1), m2 = axes.up;
  qa_vec3 org = qa_sub3(origin, pusher_origin);
  qa_vec3 rotated = qa_math_vec3(qa_dot3(org, qa_math_vec3(m0.x, m1.x, m2.x)), qa_dot3(org, qa_math_vec3(m0.y, m1.y, m2.y)), qa_dot3(org, qa_math_vec3(m0.z, m1.z, m2.z)));
  return qa_sub3(rotated, org);
}
/* TS entity.r.currentOrigin/currentAngles setters write the body. */
static void write_pose(RT *r, qa_q3_game_entity *e, qa_vec3 origin, qa_vec3 angles) {
  (void)r;
  qa_q3_base_entity_shared_set_current_origin(&e->r, origin); qa_q3_base_entity_shared_set_current_angles(&e->r, angles);
}
static qa_q3_base_actor_trace_result trace_actor(RT *r, qa_q3_base_actor_trace_query q) { return r->host.spatial->trace_actor(r->host.spatial->ctx, q); }
bool qa_q3_mover_test_entity_position(RT *r, qa_q3_game_entity *e, qa_q3_damage_participant *out) {
  owned(r, e);
  qa_vec3 start = e->client ? qa_q3_base_player_state_record_origin(&e->client->ps) : e->s.pos.base; qa_actor_id pass = qa_q3_game_entity_actor(e).id;
  qa_q3_base_actor_trace_query q; memset(&q, 0, sizeof q);
  q.start = start; q.end = start; q.shape.kind = QA_Q3_BASE_SHAPE_BOX; q.shape.mins = qa_q3_base_entity_shared_mins(&e->r); q.shape.maxs = qa_q3_base_entity_shared_maxs(&e->r);
  q.pass_actor = &pass; q.mask = e->clipmask == 0 ? 1 : e->clipmask;
  qa_q3_base_actor_trace_result t = trace_actor(r, q);
  if (t.solidity == QA_Q3_BASE_SOLID_CLEAR) return false;
  if (out) *out = t.hit_kind == QA_Q3_BASE_HIT_ACTOR ? r->host.actors.participant(r->host.actors.ctx, t.hit_actor)
      : qa_q3_participant_entity(qa_q3_entity_pool_at(pool_of(r), QA_Q3_BASE_ENTITYNUM_WORLD));
  return true;
}
typedef struct pushed_t {
  bool shared; qa_q3_shared_mover_body body;
  qa_q3_game_entity *entity; qa_vec3 origin, angles; bool has_view; double yaw;
} pushed_t;
typedef struct pushed_stack { pushed_t *items; size_t count, capacity; } pushed_stack;
static void push_saved(pushed_stack *s, pushed_t value) {
  if (s->count >= QA_Q3_MAX_GENTITIES) qa_range_error("pushed stack exceeds MAX_GENTITIES");
  if (s->count == s->capacity) {
    size_t cap = s->capacity ? s->capacity * 2 : 16; void *grown = realloc(s->items, cap * sizeof *s->items);
    if (!grown) qa_range_error("out of memory");
    s->items = grown; s->capacity = cap;
  }
  s->items[s->count++] = value;
}
static double delta_yaw(qa_q3_game_client *c) { return c->ps.delta_angles.y; }
static bool try_pushing(RT *r, qa_q3_game_entity *check, qa_q3_game_entity *pusher, qa_vec3 move, qa_vec3 amove, pushed_stack *pushed) {
  if ((qa_js_to_int32(pusher->s.e_flags) & EF_MOVER_STOP) != 0 && !qa_q3_rides(check, pusher)) return false;
  pushed_t saved; memset(&saved, 0, sizeof saved);
  saved.entity = check; saved.origin = check->client ? qa_q3_base_player_state_record_origin(&check->client->ps) : check->s.pos.base; saved.angles = check->s.apos.base;
  if (check->client) { saved.has_view = true; saved.yaw = f32(delta_yaw(check->client)); }
  push_saved(pushed, saved);
  qa_vec3 rotation_move = push_rotation(saved.origin, qa_q3_base_entity_shared_current_origin(&pusher->r), amove);
  base_origin(check, qa_add3(qa_add3(check->s.pos.base, move), rotation_move));
  if (check->client) {
    qa_q3_base_player_state *ps = &check->client->ps;
    qa_q3_base_player_state_record_set_origin(ps, qa_add3(qa_add3(qa_q3_base_player_state_record_origin(ps), move), rotation_move));
    ps->delta_angles = qa_math_vec3(ps->delta_angles.x, qa_int32(ps->delta_angles.y + angle_short(amove.y)), ps->delta_angles.z);
  }
  if (!qa_q3_rides(check, pusher)) qa_q3_lose_ground(check);
  if (!qa_q3_mover_test_entity_position(r, check, NULL)) {
    qa_q3_base_entity_shared_set_current_origin(&check->r, check->client ? qa_q3_base_player_state_record_origin(&check->client->ps) : check->s.pos.base);
    link_entity(r, check);
    return true;
  }
  /* Source riding fallback restores position and angles but retains view yaw. */
  base_origin(check, saved.origin);
  if (check->client) qa_q3_base_player_state_record_set_origin(&check->client->ps, saved.origin);
  base_angles(check, saved.angles);
  if (!qa_q3_mover_test_entity_position(r, check, NULL)) {
    qa_q3_lose_ground(check);
    pushed->count--;
    return true;
  }
  return false;
}
static bool check_proximity_position(RT *r, qa_q3_game_entity *e) {
  qa_actor_id pass = qa_q3_game_entity_actor(e).id;
  qa_q3_base_actor_trace_query q; memset(&q, 0, sizeof q);
  q.start = qa_add3(e->s.pos.base, qa_scale3(e->movedir, 0.125)); q.end = qa_add3(e->s.pos.base, qa_scale3(e->movedir, 2));
  q.shape.kind = QA_Q3_BASE_SHAPE_POINT; q.pass_actor = &pass; q.mask = 1;
  qa_q3_base_actor_trace_result t = trace_actor(r, q);
  return t.solidity == QA_Q3_BASE_SOLID_CLEAR && t.fraction == 1;
}
static bool observe(RT *r, qa_actor_id actor, qa_q3_shared_mover_body *out) {
  memset(out, 0, sizeof *out);
  if (!r->host.actors.observe(r->host.actors.ctx, actor, out)) return false;
  out->has_ground = out->state.ground != NULL;
  if (out->has_ground) out->ground = *out->state.ground;
  out->state.ground = NULL;
  return true;
}
static bool shared_position_blocked(RT *r, const qa_q3_shared_mover_body *check) {
  qa_q3_shared_mover_body body; if (!observe(r, check->actor.id, &body)) return false;
  qa_q3_base_actor_trace_query q; memset(&q, 0, sizeof q);
  q.start = body.state.origin; q.end = body.state.origin; q.shape.kind = QA_Q3_BASE_SHAPE_BOX;
  q.shape.mins = body.state.bounds.min; q.shape.maxs = body.state.bounds.max; q.pass_actor = &check->actor.id; q.mask = check->clip_mask == 0 ? 1 : check->clip_mask;
  return trace_actor(r, q).solidity != QA_Q3_BASE_SOLID_CLEAR;
}
static bool try_pushing_shared(RT *r, const qa_q3_shared_mover_body *check, qa_q3_game_entity *pusher, qa_vec3 move, qa_vec3 amove, pushed_stack *pushed) {
  bool rider = check->has_ground && qa_same_actor(check->ground, qa_q3_game_entity_actor(pusher).id);
  if ((qa_js_to_int32(pusher->s.e_flags) & EF_MOVER_STOP) != 0 && !rider) return false;
  pushed_t saved; memset(&saved, 0, sizeof saved); saved.shared = true; saved.body = *check; push_saved(pushed, saved);
  qa_vec3 rotation = push_rotation(check->state.origin, qa_q3_base_entity_shared_current_origin(&pusher->r), amove);
  const qa_actor_id *ground = rider ? &check->ground : NULL;
  r->host.actors.write(r->host.actors.ctx, check->actor, qa_add3(qa_add3(check->state.origin, move), rotation), ground);
  if (!shared_position_blocked(r, check)) { r->host.actors.link(r->host.actors.ctx, check->actor); return true; }
  r->host.actors.write(r->host.actors.ctx, check->actor, check->state.origin, ground);
  if (!shared_position_blocked(r, check)) {
    r->host.actors.write(r->host.actors.ctx, check->actor, check->state.origin, NULL);
    pushed->count--;
    return true;
  }
  return false;
}
static void restore_pushed(RT *r, pushed_stack *pushed) {
  for (size_t i = pushed->count; i-- > 0;) {
    pushed_t *saved = &pushed->items[i];
    if (saved->shared) {
      qa_q3_shared_mover_body current;
      if (observe(r, saved->body.actor.id, &current)) {
        r->host.actors.write(r->host.actors.ctx, saved->body.actor, saved->body.state.origin, current.has_ground ? &current.ground : NULL);
        r->host.actors.link(r->host.actors.ctx, saved->body.actor);
      }
      continue;
    }
    qa_q3_game_entity *e = saved->entity;
    base_origin(e, saved->origin); base_angles(e, saved->angles);
    if (e->client) {
      if (!saved->has_view) qa_range_error("pushed entity acquired a client during the transaction");
      e->client->ps.delta_angles = qa_math_vec3(e->client->ps.delta_angles.x, qa_qvm_float_to_int(saved->yaw), e->client->ps.delta_angles.z);
      qa_q3_base_player_state_record_set_origin(&e->client->ps, saved->origin);
    }
    link_entity(r, e);
  }
}
static bool push_proximity_mine(RT *r, qa_q3_game_entity *e, qa_q3_game_entity *pusher, qa_vec3 move, qa_vec3 amove) {
  qa_angle_vectors axes = qa_qvm_angle_vectors(qa_sub3(qa_math_vec3(0, 0, 0), amove));
  base_origin(e, qa_add3(e->s.pos.base, move));
  qa_vec3 org = qa_sub3(e->s.pos.base, qa_q3_base_entity_shared_current_origin(&pusher->r));
  qa_vec3 rotated = qa_math_vec3(qa_dot3(org, axes.forward), -qa_dot3(org, axes.right), qa_dot3(org, axes.up));
  base_origin(e, qa_add3(e->s.pos.base, qa_sub3(rotated, org)));
  if (!check_proximity_position(r, e)) return false;
  qa_q3_base_entity_shared_set_current_origin(&e->r, e->s.pos.base);
  link_entity(r, e);
  return true;
}
static bool outside(qa_bounds b, qa_bounds destination) {
  return b.min.x >= destination.max.x || b.min.y >= destination.max.y || b.min.z >= destination.max.z ||
      b.max.x <= destination.min.x || b.max.y <= destination.min.y || b.max.z <= destination.min.z;
}
static bool sine_pusher(qa_q3_game_entity *pusher) { return pusher->s.pos.type == QA_Q3_BASE_TR_SINE || pusher->s.apos.type == QA_Q3_BASE_TR_SINE; }
static void crush(RT *r, qa_q3_damage_participant target, qa_q3_game_entity *pusher) {
  qa_q3_damage_participant p = qa_q3_participant_entity(pusher);
  qa_q3_combat_damage(combat_of(r), target, &p, &p, NULL, NULL, 99999, 0, MOD_CRUSH);
}
static bool push_part(RT *r, qa_q3_game_entity *pusher, qa_vec3 move, qa_vec3 amove, pushed_stack *pushed, qa_q3_damage_participant *obstacle) {
  qa_bounds destination, total;
  if (qa_q3_base_entity_shared_current_angles(&pusher->r).x != 0 || qa_q3_base_entity_shared_current_angles(&pusher->r).y != 0 || qa_q3_base_entity_shared_current_angles(&pusher->r).z != 0 || amove.x != 0 || amove.y != 0 || amove.z != 0) {
    qa_vec3 min2 = qa_q3_base_entity_shared_mins(&pusher->r);
    qa_vec3 max2 = qa_q3_base_entity_shared_maxs(&pusher->r);
    qa_bounds own = {min2, max2};
    double radius = qa_radius_from_bounds(own);
    qa_vec3 extent = qa_math_vec3(radius, radius, radius), position = qa_add3(qa_q3_base_entity_shared_current_origin(&pusher->r), move);
    destination.min = qa_sub3(position, extent); destination.max = qa_add3(position, extent);
    /* Source rotating broadphase is centered on the old origin. */
    total.min = qa_sub3(destination.min, move); total.max = qa_sub3(destination.max, move);
  } else {
    qa_bounds b = {qa_q3_base_entity_shared_absmin(&pusher->r), qa_q3_base_entity_shared_absmax(&pusher->r)};
    destination.min = qa_add3(b.min, move); destination.max = qa_add3(b.max, move);
    total.min = qa_add3(b.min, qa_math_vec3(qa_js_min(move.x, 0), qa_js_min(move.y, 0), qa_js_min(move.z, 0)));
    total.max = qa_add3(b.max, qa_math_vec3(qa_js_max(move.x, 0), qa_js_max(move.y, 0), qa_js_max(move.z, 0)));
  }
  r->host.world.unlink(r->host.world.ctx, pusher->slot);
  size_t count = 0;
  const qa_actor_id *borrowed = r->host.spatial->area_actors(r->host.spatial->ctx, total, QA_Q3_MAX_GENTITIES, &count);
  qa_actor_id *entities = malloc((count ? count : 1) * sizeof *entities); if (!entities) qa_range_error("out of memory");
  if (count) memcpy(entities, borrowed, count * sizeof *entities);
  qa_vec3 origin2 = qa_add3(qa_q3_base_entity_shared_current_origin(&pusher->r), move);
  qa_vec3 angles = qa_add3(qa_q3_base_entity_shared_current_angles(&pusher->r), amove);
  write_pose(r, pusher, origin2, angles);
  link_entity(r, pusher);
  bool blocked = false;
  for (size_t i = 0; i < count && !blocked; i++) {
    qa_actor_id actor = entities[i]; qa_q3_shared_mover_body observed;
    if (!observe(r, actor, &observed) || observed.kind == QA_Q3_MOVER_BODY_ATTACHED) continue;
    qa_q3_game_entity *check = r->host.actors.native(r->host.actors.ctx, actor);
    if (!check) {
      if (observed.kind == QA_Q3_MOVER_BODY_FIXED) continue;
      bool rider = observed.has_ground && qa_same_actor(observed.ground, qa_q3_game_entity_actor(pusher).id);
      if (!rider) {
        if (outside(observed.absolute_bounds, destination)) continue;
        if (!shared_position_blocked(r, &observed)) continue;
      }
      if (try_pushing_shared(r, &observed, pusher, move, amove, pushed)) continue;
      qa_q3_damage_participant participant = r->host.actors.participant(r->host.actors.ctx, actor);
      if (sine_pusher(pusher)) { crush(r, participant, pusher); continue; }
      restore_pushed(r, pushed); *obstacle = participant; blocked = true; continue;
    }
    const char *name = qa_q3_game_entity_classname(check);
    if (r->host.has_missionpack && check->s.e_type == ET_MISSILE && name && strcmp(name, "prox mine") == 0) {
      bool clear = check->enemy == pusher ? push_proximity_mine(r, check, pusher, move, amove) : check_proximity_position(r, check);
      if (!clear) {
        check->s.loop_sound = 0;
        qa_q3_entity_pool_add_event(pool_of(r), check, QA_Q3_EV_PROXIMITY_MINE_TRIGGER, 0);
        r->host.explode_missile(r->host.ctx, check);
        if (check->activator) { qa_q3_entity_pool_free(pool_of(r), check->activator); check->activator = NULL; }
      }
      continue;
    }
    if (check->s.e_type != ET_ITEM && check->s.e_type != ET_PLAYER && !check->physics_object) continue;
    if (!qa_q3_rides(check, pusher)) {
      qa_bounds b = {qa_q3_base_entity_shared_absmin(&check->r), qa_q3_base_entity_shared_absmax(&check->r)};
      if (outside(b, destination)) continue;
      if (!qa_q3_mover_test_entity_position(r, check, NULL)) continue;
    }
    if (try_pushing(r, check, pusher, move, amove, pushed)) continue;
    if (sine_pusher(pusher)) { crush(r, qa_q3_participant_entity(check), pusher); continue; }
    restore_pushed(r, pushed); *obstacle = qa_q3_participant_entity(check); blocked = true;
  }
  free(entities);
  return blocked;
}
static void call_think_family(qa_q3_entity_callback cb, qa_q3_game_entity *self) {
  qa_q3_entity_think_fn fn; memcpy(&fn, &cb.fn, sizeof fn); fn(cb.ctx, self);
}
void qa_q3_mover_run_team(RT *r, qa_q3_game_entity *entity) {
  owned(r, entity);
  pushed_stack pushed = {NULL, 0, 0}; double time = now(r); bool blocked = false; qa_q3_damage_participant obstacle; memset(&obstacle, 0, sizeof obstacle);
  for (qa_q3_game_entity *part = entity; part; part = part->teamchain) {
    qa_vec3 a2 = qa_q3_base_evaluate_trajectory(part->s.pos, time);
    qa_vec3 b2 = qa_q3_base_entity_shared_current_origin(&part->r);
    qa_vec3 move = qa_sub3(a2, b2);
    qa_vec3 a = qa_q3_base_evaluate_trajectory(part->s.apos, time);
    qa_vec3 b = qa_q3_base_entity_shared_current_angles(&part->r);
    qa_vec3 amove = qa_sub3(a, b);
    blocked = push_part(r, part, move, amove, &pushed, &obstacle);
    if (blocked) break;
  }
  free(pushed.items);
  if (blocked) {
    double elapsed = qa_int32(time - r->host.previous_time);
    for (qa_q3_game_entity *part = entity; part; part = part->teamchain) {
      part->s.pos.time = qa_int32(part->s.pos.time + elapsed); part->s.apos.time = qa_int32(part->s.apos.time + elapsed);
      write_pose(r, part, qa_q3_base_evaluate_trajectory(part->s.pos, time), qa_q3_base_evaluate_trajectory(part->s.apos, time));
      link_entity(r, part);
    }
    if (entity->blocked.fn) { qa_q3_entity_blocked_fn fn; memcpy(&fn, &entity->blocked.fn, sizeof fn); fn(entity->blocked.ctx, entity, obstacle); }
    return;
  }
  for (qa_q3_game_entity *part = entity; part; part = part->teamchain)
    if (part->s.pos.type == QA_Q3_BASE_TR_LINEAR_STOP && time >= qa_int32(part->s.pos.time + part->s.pos.duration) && part->reached.fn) call_think_family(part->reached, part);
}
void qa_q3_mover_run(RT *r, qa_q3_game_entity *entity) {
  owned(r, entity);
  if ((qa_js_to_int32(entity->flags) & QA_Q3_GAME_FLAG_TEAMSLAVE) != 0) return;
  if (entity->s.pos.type != QA_Q3_BASE_TR_STATIONARY || entity->s.apos.type != QA_Q3_BASE_TR_STATIONARY) qa_q3_mover_run_team(r, entity);
  qa_q3_run_think(entity, now(r));
}
void qa_q3_mover_set_state(RT *r, qa_q3_game_entity *e, qa_q3_mover_state state, double time) {
  owned(r, e);
  time = qa_int32(time); e->mover_state = state;
  qa_q3_base_trajectory previous = e->s.pos;
  switch (state) {
    case QA_Q3_MOVER_POS1: e->s.pos.type = QA_Q3_BASE_TR_STATIONARY; e->s.pos.time = time; e->s.pos.base = e->pos1; break;
    case QA_Q3_MOVER_POS2: e->s.pos.type = QA_Q3_BASE_TR_STATIONARY; e->s.pos.time = time; e->s.pos.base = e->pos2; break;
    case QA_Q3_MOVER_ONE_TO_TWO: e->s.pos.type = QA_Q3_BASE_TR_LINEAR_STOP; e->s.pos.time = time; e->s.pos.base = e->pos1;
      e->s.pos.delta = qa_scale3(qa_sub3(e->pos2, e->pos1), f32(1000 / f32(previous.duration))); break;
    case QA_Q3_MOVER_TWO_TO_ONE: e->s.pos.type = QA_Q3_BASE_TR_LINEAR_STOP; e->s.pos.time = time; e->s.pos.base = e->pos2;
      e->s.pos.delta = qa_scale3(qa_sub3(e->pos1, e->pos2), f32(1000 / f32(previous.duration))); break;
    default: qa_range_error("Invalid mover state %d", (int)state);
  }
  qa_q3_base_entity_shared_set_current_origin(&e->r, qa_q3_base_evaluate_trajectory(e->s.pos, now(r)));
  link_entity(r, e);
}
void qa_q3_mover_match_team(RT *r, qa_q3_game_entity *leader, qa_q3_mover_state state, double time) {
  owned(r, leader);
  for (qa_q3_game_entity *part = leader; part; part = part->teamchain) qa_q3_mover_set_state(r, part, state, time);
}
void qa_q3_mover_return_to_pos1(RT *r, qa_q3_game_entity *e) {
  qa_q3_mover_match_team(r, e, QA_Q3_MOVER_TWO_TO_ONE, now(r));
  e->s.loop_sound = e->sound_loop;
  if (e->sound_2to1 != 0) qa_q3_entity_pool_add_event(pool_of(r), e, QA_Q3_EV_GENERAL_SOUND, e->sound_2to1);
}
void qa_q3_mover_reached_binary(RT *r, qa_q3_game_entity *e) {
  owned(r, e);
  double time = now(r); qa_q3_entity_pool *pool = pool_of(r);
  e->s.loop_sound = e->sound_loop;
  if (e->mover_state == QA_Q3_MOVER_ONE_TO_TWO) {
    qa_q3_mover_set_state(r, e, QA_Q3_MOVER_POS2, time);
    if (e->sound_pos2 != 0) qa_q3_entity_pool_add_event(pool, e, QA_Q3_EV_GENERAL_SOUND, e->sound_pos2);
    e->think = qa_q3_callback_family_resolve(&pool->callbacks.think, IMPL("reachedBinary.think"));
    qa_q3_game_entity_set_nextthink(e, qa_qvm_float_to_int(f32(f32(time) + e->wait)));
    if (!e->has_activation) { e->activation = qa_q3_participant_entity(e); e->has_activation = true; }
    r->host.use_targets(r->host.ctx, e, e->activation);
  } else if (e->mover_state == QA_Q3_MOVER_TWO_TO_ONE) {
    qa_q3_mover_set_state(r, e, QA_Q3_MOVER_POS1, time);
    if (e->sound_pos1 != 0) qa_q3_entity_pool_add_event(pool, e, QA_Q3_EV_GENERAL_SOUND, e->sound_pos1);
    if (!e->teammaster || e->teammaster == e) r->host.adjust_area_portal_state(r->host.ctx, e, false);
  } else qa_range_error("Reached_BinaryMover: bad moverState");
}
void qa_q3_mover_use_binary(RT *r, qa_q3_game_entity *e, const qa_q3_use_participant *other, const qa_q3_use_participant *activator) {
  owned(r, e);
  if ((qa_js_to_int32(e->flags) & QA_Q3_GAME_FLAG_TEAMSLAVE) != 0) {
    if (!e->teammaster) qa_range_error("Mover team slave has no team master");
    qa_q3_mover_use_binary(r, e->teammaster, other, activator); return;
  }
  double time = now(r); qa_q3_entity_pool *pool = pool_of(r);
  e->has_activation = activator != NULL; if (activator) e->activation = *activator;
  if (e->mover_state == QA_Q3_MOVER_POS1) {
    qa_q3_mover_match_team(r, e, QA_Q3_MOVER_ONE_TO_TWO, qa_int32(time + 50));
    if (e->sound_1to2 != 0) qa_q3_entity_pool_add_event(pool, e, QA_Q3_EV_GENERAL_SOUND, e->sound_1to2);
    e->s.loop_sound = e->sound_loop;
    if (!e->teammaster || e->teammaster == e) r->host.adjust_area_portal_state(r->host.ctx, e, true);
  } else if (e->mover_state == QA_Q3_MOVER_POS2) qa_q3_game_entity_set_nextthink(e, qa_qvm_float_to_int(f32(f32(time) + e->wait)));
  else {
    double total = e->s.pos.duration, elapsed = qa_int32(time - e->s.pos.time), partial = elapsed < total ? elapsed : total;
    qa_q3_mover_state state = e->mover_state == QA_Q3_MOVER_TWO_TO_ONE ? QA_Q3_MOVER_ONE_TO_TWO : QA_Q3_MOVER_TWO_TO_ONE;
    qa_q3_mover_match_team(r, e, state, qa_int32(time - qa_int32(total - partial)));
    double sound = state == QA_Q3_MOVER_ONE_TO_TWO ? e->sound_1to2 : e->sound_2to1;
    if (sound != 0) qa_q3_entity_pool_add_event(pool, e, QA_Q3_EV_GENERAL_SOUND, sound);
  }
}
static int32_t light_component(double v) { int32_t c = qa_qvm_float_to_int(f32(v * 255)); return c < 255 ? c : 255; }
void qa_q3_mover_initialize_binary(RT *r, qa_q3_game_entity *e, const qa_q3_spawn_variables *variables) {
  owned(r, e);
  qa_q3_entity_pool *pool = pool_of(r);
  if (e->model2) e->s.modelindex2 = qa_q3_config_string_registry_model_index(r->host.config, e->model2);
  qa_q3_spawn_value_string noise = qa_q3_spawn_variables_string(variables, "noise", "100");
  if (noise.present) e->s.loop_sound = qa_q3_config_string_registry_sound_index(r->host.config, noise.value);
  qa_q3_spawn_value_number light = qa_q3_spawn_variables_float(variables, "light", "100");
  qa_q3_spawn_value_vector color = qa_q3_spawn_variables_vector(variables, "color", "1 1 1");
  if (light.present || color.present) {
    int32_t intensity = qa_qvm_float_to_int(f32(light.value / 4)); if (intensity > 255) intensity = 255;
    uint32_t packed = (uint32_t)light_component(color.value.x) | ((uint32_t)light_component(color.value.y) << 8) | ((uint32_t)light_component(color.value.z) << 16) | ((uint32_t)intensity << 24);
    e->s.constant_light = (int32_t)packed;
  }
  e->use = qa_q3_callback_family_resolve(&pool->callbacks.use, IMPL("initializeBinary.use"));
  e->reached = qa_q3_callback_family_resolve(&pool->callbacks.reached, IMPL("initializeBinary.reached"));
  e->mover_state = QA_Q3_MOVER_POS1;
  e->r.sv_flags = SVF_USE_CURRENT_ORIGIN;
  e->s.e_type = ET_MOVER;
  qa_q3_base_entity_shared_set_current_origin(&e->r, e->pos1);
  link_entity(r, e);
  qa_vec3 move = qa_sub3(e->pos2, e->pos1); double distance = qa_length3(move);
  if (e->speed == 0) e->speed = 100;
  int32_t duration = qa_qvm_float_to_int(f32(f32(distance * 1000) / e->speed));
  e->s.pos.type = QA_Q3_BASE_TR_STATIONARY; e->s.pos.base = e->pos1; e->s.pos.delta = qa_scale3(move, e->speed); e->s.pos.duration = duration > 1 ? duration : 1;
}
void qa_q3_mover_blocked_door(RT *r, qa_q3_game_entity *e, qa_q3_damage_participant other) {
  owned(r, e);
  qa_q3_entity_pool *pool = pool_of(r); qa_q3_damage_participant self = qa_q3_participant_entity(e);
  if (!other.entity) {
    qa_q3_shared_mover_body body; if (!observe(r, other.actor, &body)) return;
    if (body.kind != QA_Q3_MOVER_BODY_PLAYER) {
      (void)qa_q3_entity_pool_temp_entity(pool, body.state.origin, QA_Q3_EV_ITEM_POP);
      r->host.actors.release(r->host.actors.ctx, body.actor);
      return;
    }
    if (e->damage != 0) qa_q3_combat_damage(combat_of(r), other, &self, &self, NULL, NULL, e->damage, 0, MOD_CRUSH);
    if ((qa_js_to_int32(e->spawnflags) & 4) == 0) qa_q3_mover_use_binary(r, e, &self, &other);
    return;
  }
  qa_q3_game_entity *o = other.entity; owned(r, o);
  if (!o->client) {
    if (o->s.e_type == ET_ITEM) {
      if (!o->item) qa_range_error("Blocked item has no item definition");
      if (o->item->type == QA_Q3_BASE_IT_TEAM) { r->host.return_dropped_flag(r->host.ctx, o); return; }
    }
    (void)qa_q3_entity_pool_temp_entity(pool, o->s.origin, QA_Q3_EV_ITEM_POP);
    qa_q3_entity_pool_free(pool, o);
    return;
  }
  if (e->damage != 0) qa_q3_combat_damage(combat_of(r), other, &self, &self, NULL, NULL, e->damage, 0, MOD_CRUSH);
  if ((qa_js_to_int32(e->spawnflags) & 4) != 0) return;
  qa_q3_mover_use_binary(r, e, &self, &other);
}
static qa_q3_entity_callback mkcb(const void *fn, void *ctx) { qa_q3_entity_callback cb; memcpy(&cb.fn, fn, sizeof cb.fn); cb.ctx = ctx; return cb; }
static void cb_return(void *ctx, qa_q3_game_entity *self) { qa_q3_mover_return_to_pos1(ctx, self); }
static void cb_use(void *ctx, qa_q3_game_entity *self, const qa_q3_use_participant *other, const qa_q3_use_participant *activator) { qa_q3_mover_use_binary(ctx, self, other, activator); }
static void cb_reached(void *ctx, qa_q3_game_entity *self) { qa_q3_mover_reached_binary(ctx, self); }
void qa_q3_mover_bind_save_callbacks(RT *r) {
  qa_q3_callback_catalog *cat = &pool_of(r)->callbacks;
  qa_q3_entity_think_fn back = cb_return, reached = cb_reached; qa_q3_entity_use_fn use = cb_use;
  qa_q3_callback_family_intern(&cat->think, IMPL("reachedBinary.think"), mkcb(&back, r));
  qa_q3_callback_family_intern(&cat->use, IMPL("initializeBinary.use"), mkcb(&use, r));
  qa_q3_callback_family_intern(&cat->reached, IMPL("initializeBinary.reached"), mkcb(&reached, r));
}
void qa_q3_mover_runtime_bind(RT *r) {
  if (r->bound) return;
  qa_q3_game_combat_context *c = combat_of(r);
  if (c->entities->options.product != c->product) qa_range_error("Mover product does not match its entity pool");
  qa_q3_mover_bind_save_callbacks(r); r->bound = true;
}
void qa_q3_mover_runtime_init(RT *r, qa_q3_mover_host host) {
  memset(r, 0, sizeof *r); r->host = host;
  if (host.combat) qa_q3_mover_runtime_bind(r);
}
