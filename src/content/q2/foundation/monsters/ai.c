/* Port of src/content/q2/foundation/monsters/ai.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/ai.h"

#include <float.h>
#include <math.h>

#include "monsters_internal.h"
#include "qa/content/q2/foundation/monsters/alternate_fly.h"

double qa_q2m_monster_solid_mask(qa_q2f_game *game) {
  return (double)((uint32_t)QA_Q2M_MASK_MONSTERSOLID | (q2m_rr(game) ? 0x40000000u : 0u));
}
double qa_q2m_attack_trace_mask(qa_q2f_game *game) {
  return (double)((uint32_t)QA_Q2M_MASK_SHOT | (q2m_rr(game) ? 0x40000000u : 0u));
}
double qa_q2m_health(qa_q2f_game *game, const qa_actor_id *actor) {
  if (!actor) return 0;
  qa_combat_state_result r = game->host->combat.read(game->host->combat.ctx, *actor);
  return r.has_value ? r.value.health : 0;
}
bool qa_q2m_enemy_body(qa_q2m_context *c, qa_body_state *out) {
  if (!c->entity->has_enemy) return false;
  qa_body_state_result r = c->game->host->bodies.read(c->game->host->bodies.ctx, c->entity->enemy);
  if (!r.has_value) return false;
  *out = r.value;
  return true;
}
bool qa_q2m_enemy_eye(qa_q2m_context *c, qa_vec3 *out) {
  qa_body_state body;
  if (!qa_q2m_enemy_body(c, &body)) return false;
  qa_q2f_target_obs_result observed = c->game->monster_target(c->game->ctx, &c->entity->enemy);
  if (!observed.has_value) return false;
  *out = body.origin;
  out->z = body.origin.z + observed.value.view_height;
  return true;
}
double qa_q2m_target_distance(qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return INFINITY;
  qa_body_state self = q2m_body(c->game, c->entity);
  if (!q2m_rr(c->game)) return qa_hypot3(qa_sub3_double(enemy.origin, self.origin));
  double dx = fmax(0, fmax(enemy.origin.x + enemy.bounds.min.x - self.origin.x - self.bounds.max.x,
                           self.origin.x + self.bounds.min.x - enemy.origin.x - enemy.bounds.max.x));
  double dy = fmax(0, fmax(enemy.origin.y + enemy.bounds.min.y - self.origin.y - self.bounds.max.y,
                           self.origin.y + self.bounds.min.y - enemy.origin.y - enemy.bounds.max.y));
  double dz = fmax(0, fmax(enemy.origin.z + enemy.bounds.min.z - self.origin.z - self.bounds.max.z,
                           self.origin.z + self.bounds.min.z - enemy.origin.z - enemy.bounds.max.z));
  return sqrt(dx * dx + dy * dy + dz * dz);
}
bool qa_q2m_visible(qa_q2m_context *c, const qa_actor_id *actor) {
  if (!actor) return false;
  qa_body_state_result target = c->game->host->bodies.read(c->game->host->bodies.ctx, *actor);
  if (!target.has_value) return false;
  qa_vec3 origin = q2m_body(c->game, c->entity).origin;
  qa_vec3 start = origin;
  start.z = origin.z + c->entity->view_height;
  qa_q2f_target_obs_result observed = c->game->monster_target(c->game->ctx, actor);
  if (!observed.has_value) return false;
  qa_vec3 end = target.value.origin;
  end.z = target.value.origin.z + observed.value.view_height;
  return q2m_trace(c->game, start, end, NULL, &c->entity->actor.id, QA_Q2M_MASK_OPAQUE).fraction == 1;
}
bool qa_q2m_visible_enemy(qa_q2m_context *c) {
  return qa_q2m_visible(c, c->entity->has_enemy ? &c->entity->enemy : NULL);
}
bool qa_q2m_in_front(qa_q2m_context *c, qa_actor_id actor) {
  qa_body_state_result body = c->game->host->bodies.read(c->game->host->bodies.ctx, actor);
  if (!body.has_value) return false;
  qa_body_state self = q2m_body(c->game, c->entity);
  return qa_dot3_double(qa_math_angle_vectors_rule(self.angles, QA_ANGLE_RULE_NATIVE).forward,
                        qa_normalize3_double_hypot(qa_sub3_double(body.value.origin, self.origin))) > 0.3;
}
qa_vec3 qa_q2m_project_flash(qa_q2m_context *c, qa_vec3 offset, qa_vec3 angles) {
  qa_body_state body = q2m_body(c->game, c->entity);
  qa_angle_vectors axes = qa_math_angle_vectors_rule(angles, QA_ANGLE_RULE_NATIVE);
  double size = q2m_rr(c->game) ? c->entity->scale : 1;
  return qa_add3_double(body.origin, qa_add3_double(qa_scale3_double(axes.forward, offset.x * size),
      qa_add3_double(qa_scale3_double(axes.right, offset.y * size), q2m_v3(0, 0, offset.z * size))));
}
qa_vec3 qa_q2m_project_flash_default(qa_q2m_context *c, qa_vec3 offset) {
  return qa_q2m_project_flash(c, offset, q2m_body(c->game, c->entity).angles);
}
static bool clear_to(qa_q2m_context *c, qa_vec3 start, qa_vec3 end, double mask, bool rr) {
  qa_trace_result trace = q2m_trace(c->game, start, end, NULL, &c->entity->actor.id, mask);
  bool hit_enemy = trace.hit.kind == QA_TRACE_HIT_ACTOR
    && ((c->entity->has_enemy && qa_same_actor(trace.hit.actor, c->entity->enemy))
        || (rr && c->game->host->is_player(c->game->host->ctx, trace.hit.actor)));
  return hit_enemy || (rr ? trace.fraction > 0.8 && !trace.start_solid : trace.fraction == 1);
}
bool qa_q2m_clear_shot(qa_q2m_context *c, qa_vec3 offset) {
  qa_vec3 eye;
  if (!qa_q2m_enemy_eye(c, &eye)) return false;
  bool rr = q2m_rr(c->game);
  qa_vec3 angles = q2m_body(c->game, c->entity).angles;
  qa_vec3 used = rr ? q2m_v3(angles.x, c->state->ideal_yaw, 0) : angles;
  qa_vec3 start = qa_q2m_project_flash(c, offset, used);
  bool blind = rr && (c->state->attack_state == QA_Q2M_ATTACK_BLIND || c->state->manual_steering || c->state->lost_sight);
  double mask = rr ? (double)(0x42000003u | 0x4000u) : QA_Q2M_MASK_SHOT;
  if (clear_to(c, start, blind ? c->state->blind_fire_target : eye, mask, rr)) return true;
  qa_body_state enemy;
  return rr && !blind && qa_q2m_enemy_body(c, &enemy) && clear_to(c, start, enemy.origin, mask, rr);
}
bool qa_q2m_trace_ground_actor(qa_trace_result trace, qa_q2f_game *game, qa_actor_id *out) {
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR) { *out = trace.hit.actor; return true; }
  if (trace.hit.kind == QA_TRACE_HIT_WORLD) { *out = game->host->world_actor(game->host->ctx); return true; }
  return false;
}
void qa_q2m_change_yaw(qa_q2m_context *c) {
  qa_body_state body = q2m_body(c->game, c->entity);
  double current = fmod(fmod(body.angles.y, 360) + 360, 360);
  double move = c->state->ideal_yaw - current;
  if (move > 180) move -= 360;
  if (move < -180) move += 360;
  double speed = c->state->yaw_speed * (q2m_rr(c->game) ? c->game->host->frame_seconds(c->game->host->ctx) * 10 : 1);
  move = fmax(-speed, fmin(speed, move));
  qa_q2f_body_changes changes;
  memset(&changes, 0, sizeof changes);
  changes.has_angles = true;
  changes.angles = body.angles;
  changes.angles.y = fmod(fmod(current + move, 360) + 360, 360);
  q2m_move(c->game, c->entity, changes, true, false);
}
void qa_q2m_face_enemy(qa_q2m_context *c) {
  qa_body_state enemy;
  qa_vec3 target;
  bool has = true;
  if (c->state->manual_steering) target = c->state->blind_fire_target;
  else if (qa_q2m_enemy_body(c, &enemy)) target = enemy.origin;
  else has = false;
  if (!has) return;
  c->state->ideal_yaw = qa_vector_to_angles_with_rule(qa_sub3_double(target, q2m_body(c->game, c->entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
  qa_q2m_change_yaw(c);
}
bool qa_q2m_check_bottom(qa_q2m_context *c, qa_vec3 origin) {
  qa_body_state body = q2m_body(c->game, c->entity);
  qa_vec3 minimum = qa_add3_double(origin, body.bounds.min), maximum = qa_add3_double(origin, body.bounds.max);
  bool ceiling = c->entity->gravity_vector.z > 0;
  double direction = ceiling ? 1 : -1;
  double support = ceiling ? maximum.z : minimum.z;
  qa_vec3 corners[4] = {
    q2m_v3(minimum.x, minimum.y, support + direction), q2m_v3(minimum.x, maximum.y, support + direction),
    q2m_v3(maximum.x, minimum.y, support + direction), q2m_v3(maximum.x, maximum.y, support + direction),
  };
  bool all_solid = true;
  for (int i = 0; i < 4; i++) if (c->game->host->point_contents(c->game->host->ctx, corners[i]) != 1) all_solid = false;
  if (all_solid) return true;
  bool rr = q2m_rr(c->game);
  qa_vec3 center = q2m_v3((minimum.x + maximum.x) * 0.5, (minimum.y + maximum.y) * 0.5, support);
  qa_vec3 start = rr ? q2m_v3(origin.x, origin.y, support) : center;
  qa_bounds middle_bounds;
  const qa_bounds *mb = NULL;
  if (rr) {
    middle_bounds = body.bounds; middle_bounds.min.z = 0; middle_bounds.max.z = 0; mb = &middle_bounds;
  }
  qa_vec3 middle_end = start; middle_end.z = start.z + direction * 36;
  qa_trace_result middle = q2m_trace(c->game, start, middle_end, mb, &c->entity->actor.id, qa_q2m_monster_solid_mask(c->game));
  if (middle.fraction == 1) return false;
  if (rr && ((uint32_t)c->entity->spawnflags & 131072u) != 0) return true;
  qa_vec3 quadrant = q2m_v3((maximum.x - minimum.x) * 0.25, (maximum.y - minimum.y) * 0.25, 0);
  for (int i = 0; i < 4; i++) {
    qa_vec3 point;
    qa_bounds corner_bounds;
    const qa_bounds *cb = NULL;
    if (rr) {
      point = q2m_v3(center.x + (corners[i].x == minimum.x ? -quadrant.x : quadrant.x),
                     center.y + (corners[i].y == minimum.y ? -quadrant.y : quadrant.y), support);
      corner_bounds.min = qa_scale3_double(quadrant, -1); corner_bounds.max = quadrant; cb = &corner_bounds;
    } else { point = corners[i]; point.z = support; }
    qa_vec3 end = point; end.z = point.z + direction * 36;
    qa_trace_result trace = q2m_trace(c->game, point, end, cb, &c->entity->actor.id, qa_q2m_monster_solid_mask(c->game));
    if (trace.fraction == 1 || (trace.end.z - middle.end.z) * direction > 18) return false;
  }
  return true;
}

static bool source_move_step(qa_q2m_context *c, double yaw, double distance, bool commit, bool relink) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2m_body(game, entity);
  if (state->locomotion == QA_Q2M_LOCO_STATIONARY) return false;
  if (body.ground == NULL && state->locomotion == QA_Q2M_LOCO_WALK) return false;
  double radians = yaw * 3.141592653589793 / 180;
  qa_q2m_before_move_result step = c->before_source_move(c->impl_ctx, c, q2m_v3(cos(radians) * distance, sin(radians) * distance, 0));
  if (step.handled) return true;
  qa_vec3 destination = qa_add3_double(body.origin, step.displacement);
  if (state->locomotion == QA_Q2M_LOCO_FLY || state->locomotion == QA_Q2M_LOCO_SWIM) {
    if (q2m_rr(game) && state->alt.alternate_fly && qa_q2m_alternate_fly_step(c)) return true;
    qa_vec3 feet1 = body.origin; feet1.z = body.origin.z + body.bounds.min.z + 1;
    qa_vec3 feet27 = body.origin; feet27.z = body.origin.z + body.bounds.min.z + 27;
    bool wet = ((uint32_t)game->host->point_contents(game->host->ctx, feet1) & (uint32_t)QA_Q2M_MASK_WATER) != 0;
    bool deep = ((uint32_t)game->host->point_contents(game->host->ctx, feet27) & (uint32_t)QA_Q2M_MASK_WATER) != 0;
    for (int attempt = 0; attempt < 2; attempt++) {
      qa_vec3 end = destination;
      if (attempt == 0 && entity->has_enemy) {
        if (!entity->has_goal) { entity->goal = entity->enemy; entity->has_goal = true; }
        qa_body_state_result goal = game->host->bodies.read(game->host->bodies.ctx, entity->goal);
        if (goal.has_value) {
          double dz = body.origin.z - goal.value.origin.z;
          double stride = q2m_rr(game) ? game->host->frame_seconds(game->host->ctx) * 80 : 8;
          if (game->host->is_player(game->host->ctx, entity->goal)) {
            if (dz > 40) end.z -= stride;
            if (dz < 30 && !(state->locomotion == QA_Q2M_LOCO_SWIM && !deep)) end.z += stride;
          } else end.z += dz > stride ? -stride : dz > 0 ? -dz : dz < -stride ? stride : dz;
        }
      }
      qa_trace_result trace = q2m_trace(game, body.origin, end, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game));
      qa_vec3 enter = trace.end; enter.z = trace.end.z + body.bounds.min.z + 1;
      bool enters_water = ((uint32_t)game->host->point_contents(game->host->ctx, enter) & (uint32_t)QA_Q2M_MASK_WATER) != 0;
      if ((state->locomotion == QA_Q2M_LOCO_FLY && !wet && enters_water)
          || (state->locomotion == QA_Q2M_LOCO_SWIM && !deep && !enters_water)) return false;
      if (trace.fraction == 1 && !trace.start_solid && !trace.all_solid) {
        if (commit) {
          qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
          changes.has_origin = true; changes.origin = trace.end;
          q2m_move(game, entity, changes, true, relink);
          if (relink) game->host->touch_triggers(game->host->ctx, entity->actor);
        }
        return true;
      }
      if (!entity->has_enemy) break;
    }
    return false;
  }
  qa_vec3 gravity = entity->gravity_vector;
  bool ceiling = gravity.z > 0;
  qa_vec3 start = qa_add3_double(destination, qa_scale3_double(gravity, -18));
  qa_vec3 end = qa_add3_double(start, qa_scale3_double(gravity, 36));
  qa_trace_result trace = q2m_trace(game, start, end, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game));
  if (trace.all_solid) return false;
  if (trace.start_solid) {
    qa_vec3 retry = start;
    if (!q2m_rr(game)) retry.z = start.z - 18; else retry = destination;
    trace = q2m_trace(game, retry, end, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game));
    if (trace.start_solid || trace.all_solid) return false;
  }
  double support_offset = ceiling ? body.bounds.max.z - 1 : body.bounds.min.z + 1;
  qa_vec3 feet = body.origin; feet.z = body.origin.z + support_offset;
  qa_vec3 end_feet = trace.end; end_feet.z = trace.end.z + support_offset;
  if (((uint32_t)game->host->point_contents(game->host->ctx, feet) & (uint32_t)QA_Q2M_MASK_WATER) == 0
      && ((uint32_t)game->host->point_contents(game->host->ctx, end_feet) & (uint32_t)QA_Q2M_MASK_WATER) != 0) return false;
  if (trace.fraction == 1) {
    if (((uint32_t)entity->flags & 256u) == 0) return false;
    if (commit) {
      qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_origin = true; changes.origin = destination;
      changes.has_ground = true; changes.ground = NULL;
      q2m_move(game, entity, changes, true, relink);
      if (relink) game->host->touch_triggers(game->host->ctx, entity->actor);
    }
    return true;
  }
  if (!c->accepts_source_ground_move(c->impl_ctx, c, trace.end)) return false;
  if (!qa_q2m_check_bottom(c, trace.end)) {
    if (((uint32_t)entity->flags & 256u) == 0) return false;
    if (commit) {
      qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_origin = true; changes.origin = trace.end;
      q2m_move(game, entity, changes, true, relink);
      if (relink) game->host->touch_triggers(game->host->ctx, entity->actor);
    }
    return true;
  }
  if (commit) {
    entity->flags = (double)((uint32_t)entity->flags & ~256u);
    qa_actor_id ground;
    bool has_ground = qa_q2m_trace_ground_actor(trace, game, &ground);
    qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_origin = true; changes.origin = trace.end;
    changes.has_ground = true; changes.ground = has_ground ? &ground : NULL;
    q2m_move(game, entity, changes, true, relink);
    if (relink) game->host->touch_triggers(game->host->ctx, entity->actor);
  }
  return true;
}
bool qa_q2m_walk_move(qa_q2m_context *c, double yaw, double distance, bool commit, bool relink) {
  bool moved = source_move_step(c, yaw, distance, commit, relink);
  if (commit && relink) (void)c->consume_source_blocked(c->impl_ctx, c);
  return moved;
}
bool qa_q2m_step_direction(qa_q2m_context *c, double yaw, double distance) {
  c->state->ideal_yaw = yaw;
  qa_q2m_change_yaw(c);
  qa_body_state previous = q2m_body(c->game, c->entity);
  if (!qa_q2m_walk_move(c, yaw, distance, true, false)) {
    c->game->link(c->game->ctx, c->entity);
    c->game->host->touch_triggers(c->game->host->ctx, c->entity->actor);
    return !q2m_live(c->game, c->entity->actor.id);
  }
  (void)c->consume_source_blocked(c->impl_ctx, c);
  if (!q2m_live(c->game, c->entity->actor.id)) return true;
  double delta = fmod(fmod(q2m_body(c->game, c->entity).angles.y - c->state->ideal_yaw, 360) + 360, 360);
  if (delta > 45 && delta < 315
      && !(c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE
           && strncmp(c->entity->classname, "monster_widow", 13) == 0)) {
    qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_origin = true; changes.origin = previous.origin;
    q2m_move(c->game, c->entity, changes, true, false);
  }
  c->game->link(c->game->ctx, c->entity);
  c->game->host->touch_triggers(c->game->host->ctx, c->entity->actor);
  return true;
}
bool qa_q2m_chase_direction(qa_q2m_context *c, qa_vec3 goal, double distance) {
  double old = trunc(c->state->ideal_yaw / 45) * 45;
  double turnaround = fmod(old - 180 + 360, 360);
  qa_vec3 delta = qa_sub3_double(goal, q2m_body(c->game, c->entity).origin);
  double x = delta.x > 10 ? 0 : delta.x < -10 ? 180 : -1;
  double y = delta.y < -10 ? 270 : delta.y > 10 ? 90 : -1;
  if (x != -1 && y != -1) {
    double diagonal = x == 0 ? (y == 90 ? 45 : 315) : (y == 90 ? 135 : 215);
    if (diagonal != turnaround && qa_q2m_step_direction(c, diagonal, distance)) return true;
  }
  double direction_roll = floor(q2m_rnd(c->game) * 4);
  bool rogue = c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE;
  if ((rogue ? ((int32_t)direction_roll & 1) != 0 : direction_roll != 0) || fabs(delta.y) > fabs(delta.x)) {
    double tmp = x; x = y; y = tmp;
  }
  if (x != -1 && x != turnaround && qa_q2m_step_direction(c, x, distance)) return true;
  if (y != -1 && y != turnaround && qa_q2m_step_direction(c, y, distance)) return true;
  if (rogue && q2m_live(c->game, c->entity->actor.id)
      && qa_q2m_health(c->game, &c->entity->actor.id) > 0 && c->blocked(c->impl_ctx, c, distance)) return true;
  if (old != -1 && qa_q2m_step_direction(c, old, distance)) return true;
  bool descending = floor(q2m_rnd(c->game) * 2) == 0;
  for (int index = 0; index < 8; index++) {
    double yaw = descending ? 315 - index * 45 : index * 45;
    if (yaw != turnaround && qa_q2m_step_direction(c, yaw, distance)) return true;
  }
  if (turnaround != -1 && qa_q2m_step_direction(c, turnaround, distance)) return true;
  c->state->ideal_yaw = old;
  if (!qa_q2m_check_bottom(c, q2m_body(c->game, c->entity).origin))
    c->entity->flags = (double)((uint32_t)c->entity->flags | 256u);
  return false;
}

static void slide(qa_q2m_context *c, double distance) {
  qa_q2m_state *state = c->state;
  qa_q2f_game *game = c->game;
  if (!state->manual_steering) qa_q2m_face_enemy(c);
  double side = state->lefty ? 90 : -90;
  double amount = q2m_rr(game) && state->locomotion != QA_Q2M_LOCO_FLY
    ? fmin(distance, 8 / (game->host->frame_seconds(game->host->ctx) * 100)) : distance;
  if (qa_q2m_walk_move(c, state->ideal_yaw + side, amount, true, true)) return;
  bool extended_rules = q2m_rr(game) || c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE;
  if (extended_rules && state->dodging) {
    qa_q2m_finish_dodge(c); state->attack_state = QA_Q2M_ATTACK_STRAIGHT; return;
  }
  state->lefty = !state->lefty;
  if (!qa_q2m_walk_move(c, state->ideal_yaw - side, amount, true, true) && extended_rules)
    state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
}

void qa_q2m_run_ai(qa_q2m_context *c, qa_q2m_ai_kind ai, double distance) {
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  bool rogue = c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE;
  bool rr = q2m_rr(game);
  bool extended = rr || rogue;
  if (ai == QA_Q2M_AI_NONE || ai == QA_Q2M_AI_SOURCE) return;
  if (ai == QA_Q2M_AI_TURN) {
    if (distance != 0) (void)qa_q2m_walk_move(c, q2m_body(game, entity).angles.y, distance, true, true);
    if (!q2m_live(game, entity->actor.id)) return;
    if (c->find_target(c->impl_ctx, c)) return;
    if (!rr || !state->manual_steering) qa_q2m_change_yaw(c);
    return;
  }
  if (ai == QA_Q2M_AI_MOVE || ai == QA_Q2M_AI_SOLDIER_MOVE) {
    (void)qa_q2m_walk_move(c, q2m_body(game, entity).angles.y, distance, true, true);
    if (!q2m_live(game, entity->actor.id)) return;
    if (ai == QA_Q2M_AI_SOLDIER_MOVE && !qa_q2m_prone_shot(c)) c->dispatch(c->impl_ctx, c, "soldier_stand_up");
    return;
  }
  if (ai == QA_Q2M_AI_CHARGE) {
    qa_body_state enemy;
    bool has_enemy = qa_q2m_enemy_body(c, &enemy);
    if (extended && !has_enemy) return;
    if (rogue && !rr && has_enemy && qa_q2m_visible_enemy(c)) state->blind_fire_target = enemy.origin;
    if (!state->manual_steering) qa_q2m_face_enemy(c); else qa_q2m_change_yaw(c);
    if (rr && has_enemy && qa_q2m_visible_enemy(c))
      state->blind_fire_target = qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, -0.1));
    if (distance != 0) {
      if (extended && state->charging) { (void)c->move_to_goal(c->impl_ctx, c, distance); return; }
      double yaw = q2m_body(game, entity).angles.y;
      if (extended && state->attack_state == QA_Q2M_ATTACK_SLIDING) {
        qa_q2f_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
        double side = rogue && !rr && enemy_entity && strcmp(enemy_entity->classname, "tesla") == 0 ? 0 : state->lefty ? 90 : -90;
        double sideways = !rr ? distance : distance * state->move->sidestep_scale;
        if (!qa_q2m_walk_move(c, state->ideal_yaw + side, sideways, true, true)) {
          state->lefty = !state->lefty;
          (void)qa_q2m_walk_move(c, state->ideal_yaw - side, sideways, true, true);
        }
      } else (void)qa_q2m_walk_move(c, yaw, distance, true, true);
    }
    if (rr && qa_q2m_target_distance(c) <= 50) qa_q2m_change_yaw(c);
    return;
  }
  if (ai == QA_Q2M_AI_STAND) {
    if (distance != 0) (void)qa_q2m_walk_move(c, q2m_body(game, entity).angles.y, distance, true, true);
    if (!q2m_live(game, entity->actor.id)) return;
    if (state->stand_ground) {
      if (entity->has_enemy) {
        qa_body_state enemy;
        bool has_enemy = qa_q2m_enemy_body(c, &enemy);
        if (has_enemy) state->ideal_yaw = qa_vector_to_angles_with_rule(
            qa_sub3_double(enemy.origin, q2m_body(game, entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
        if (q2m_body(game, entity).angles.y != state->ideal_yaw && state->temporary_stand_ground) {
          state->stand_ground = false; state->temporary_stand_ground = false; c->run(c->impl_ctx, c);
        }
        if (!rogue || !state->manual_steering) qa_q2m_change_yaw(c);
        bool attacking = c->check_attack(c->impl_ctx, c, 0);
        if (rogue) {
          qa_body_state target;
          if (qa_q2m_enemy_body(c, &target) && qa_q2m_visible_enemy(c)) {
            state->lost_sight = false; state->last_sighting = target.origin; state->blind_fire_target = target.origin;
            state->trail_time = q2m_now(game); state->blind_fire_delay = 0;
          } else if (!attacking) (void)c->find_target(c->impl_ctx, c);
        }
      } else (void)c->find_target(c->impl_ctx, c);
      return;
    }
    if (c->find_target(c->impl_ctx, c)) return;
    if (q2m_now(game) > state->pause_time) { c->walk(c->impl_ctx, c); return; }
    if (state->has_idle && ((uint32_t)entity->spawnflags & 1u) == 0 && q2m_now(game) > state->idle_time) {
      if (state->idle_time != 0) { c->idle(c->impl_ctx, c); state->idle_time = q2m_now(game) + 15 + q2m_rnd(game) * 15; }
      else state->idle_time = q2m_now(game) + q2m_rnd(game) * 15;
    }
    return;
  }
  if (ai == QA_Q2M_AI_WALK) {
    (void)c->move_to_goal(c->impl_ctx, c, distance);
    if (!q2m_live(game, entity->actor.id)) return;
    if (c->find_target(c->impl_ctx, c)) return;
    if (state->has_search && q2m_now(game) > state->idle_time) {
      if (state->idle_time != 0) { c->search(c->impl_ctx, c); state->idle_time = q2m_now(game) + 15 + q2m_rnd(game) * 15; }
      else state->idle_time = q2m_now(game) + q2m_rnd(game) * 15;
    }
    return;
  }
  /* "run" */
  if (state->combat_point) { (void)c->move_to_goal(c->impl_ctx, c, distance); return; }
  if (rogue && !rr) {
    state->ducked = false;
    qa_body_state body = q2m_body(game, entity);
    if (body.bounds.max.z != state->normal_height) {
      state->can_take_damage = true; state->next_duck_time = q2m_now(game) + 0.5;
      qa_q2f_combat_traits traits; memset(&traits, 0, sizeof traits);
      traits.has_can_take_damage = true; traits.can_take_damage = true;
      game->host->combat.set_traits(game->host->combat.ctx, entity->actor, traits);
      qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_bounds = true; changes.bounds = body.bounds; changes.bounds.max.z = state->normal_height;
      q2m_move(game, entity, changes, false, false);
    }
  }
  if (c->run_hint_path(c->impl_ctx, c, distance)) return;
  bool already_moved = false;
  if (state->has_sound_target) {
    qa_vec3 delta = qa_sub3_double(q2m_body(game, entity).origin, state->sound_target.origin);
    if (qa_hypot3(delta) < (!rr ? 64 : 32)) {
      state->stand_ground = true; state->temporary_stand_ground = true; c->stand(c->impl_ctx, c); return;
    }
    (void)c->move_to_goal(c->impl_ctx, c, distance);
    already_moved = extended;
    if (!q2m_live(game, entity->actor.id)) return;
    if (!c->find_target(c->impl_ctx, c)) return;
  }
  bool attacking = c->check_attack(c->impl_ctx, c, distance);
  if (extended) {
    if (!qa_q2m_visible_enemy(c) && state->attack_state == QA_Q2M_ATTACK_SLIDING) state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
    if (state->dodging) state->attack_state = QA_Q2M_ATTACK_SLIDING;
  } else if (attacking) return;
  if (state->attack_state == QA_Q2M_ATTACK_SLIDING) {
    if (!already_moved) slide(c, distance);
    if (!attacking && state->attack_state == QA_Q2M_ATTACK_SLIDING) return;
  } else if (state->charging && !state->manual_steering) qa_q2m_face_enemy(c);
  if (attacking) {
    if (distance != 0 && !already_moved && state->attack_state == QA_Q2M_ATTACK_STRAIGHT && !state->stand_ground)
      (void)c->move_to_goal(c->impl_ctx, c, distance);
    return;
  }
  if (state->stand_ground) return;
  if (!qa_q2m_visible_enemy(c) && c->check_lost_hint_path(c->impl_ctx, c)) return;
  if (!qa_q2m_visible_enemy(c) && strcmp(game->options.mode, "coop") == 0 && c->find_target(c->impl_ctx, c)) return;
  if (!already_moved) (void)c->move_to_goal(c->impl_ctx, c, distance);
  if (!q2m_live(game, entity->actor.id)) return;
  if (state->search_time != 0 && q2m_now(game) > state->search_time + 20) state->search_time = 0;
}

bool qa_q2m_prone_shot(qa_q2m_context *c) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(c, &enemy)) return false;
  if (qa_q2m_health(c->game, c->entity->has_enemy ? &c->entity->enemy : NULL) <= 0) return false;
  qa_body_state self = q2m_body(c->game, c->entity);
  qa_vec3 difference = qa_sub3_double(enemy.origin, self.origin);
  difference.z = 0;
  return qa_dot3_double(qa_math_angle_vectors_rule(self.angles, QA_ANGLE_RULE_NATIVE).forward,
                        qa_normalize3_double_hypot(difference)) >= 0.8;
}
void qa_q2m_set_duck(qa_q2m_context *c, bool down) {
  if (!down && !c->state->ducked) return;
  qa_body_state body = q2m_body(c->game, c->entity);
  c->state->ducked = down;
  qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
  changes.has_bounds = true; changes.bounds = body.bounds;
  changes.bounds.max.z = c->state->normal_height - (down ? 32 : 0);
  q2m_move(c->game, c->entity, changes, false, false);
}
void qa_q2m_finish_dodge(qa_q2m_context *c) {
  c->state->dodging = false;
  if (c->state->attack_state == QA_Q2M_ATTACK_SLIDING) c->state->attack_state = QA_Q2M_ATTACK_STRAIGHT;
}
static void set_flies(qa_q2m_context *c, bool on);
static bool wet_corpse(qa_q2m_context *c) {
  qa_body_state body = q2m_body(c->game, c->entity);
  qa_vec3 point = body.origin; point.z = body.origin.z + body.bounds.min.z + 1;
  return ((uint32_t)c->game->host->point_contents(c->game->host->ctx, point) & (uint32_t)QA_Q2M_MASK_WATER) != 0;
}
void qa_q2m_fly_check(qa_q2m_context *c) {
  if (wet_corpse(c) || q2m_rnd(c->game) > 0.5) return;
  c->schedule(c->impl_ctx, c, 5 + 10 * q2m_rnd(c->game), "M_FliesOn");
}
void qa_q2m_corpse(qa_q2m_context *c) {
  c->state->corpse = true;
  c->entity->server_flags = (double)((uint32_t)c->entity->server_flags | 2u);
  qa_q2f_body_changes changes; memset(&changes, 0, sizeof changes);
  changes.has_bounds = true;
  changes.bounds = (qa_bounds){q2m_v3(-16, -16, -24), q2m_v3(16, 16, -8)};
  q2m_move(c->game, c->entity, changes, false, false);
  c->game->motion(c->game->ctx, c->entity, QA_Q2F_MOTION_TOSS);
  c->game->cancel(c->game->ctx, c->entity);
  if (!q2m_rr(c->game)) {
    if (strcmp(c->state->kind, "infantry") == 0) qa_q2m_fly_check(c);
  } else c->schedule(c->impl_ctx, c, 0.1, "monster_dead_think");
}
static void set_flies(qa_q2m_context *c, bool on) {
  if (on) c->entity->effects = (double)((uint32_t)c->entity->effects | 0x4000u);
  else c->entity->effects = (double)((uint32_t)c->entity->effects & ~0x4000u);
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_SOUND; e.actor = &c->entity->actor.id;
  e.origin = q2m_body(c->game, c->entity).origin; e.path = "infantry/inflies1.wav";
  e.channel = 0; e.volume = 1; e.attenuation = 1; e.loop = on ? "start" : "stop";
  c->game->host->emit(c->game->host->ctx, e);
  c->game->show(c->game->ctx, c->entity);
}
void qa_q2m_monster_dead_think(qa_q2m_context *c) {
  qa_q2m_state *state = c->state;
  qa_q2f_game *game = c->game;
  qa_q2f_entity *entity = c->entity;
  if (strcmp(state->kind, "infantry") == 0) {
    if (!state->has_flies_time) { state->has_flies_time = true; state->flies_time = q2m_now(game) + 5 + q2m_rnd(game) * 10; }
    else if (state->flies_time < q2m_now(game)) {
      bool on = ((uint32_t)entity->effects & 0x4000u) == 0;
      set_flies(c, on);
      state->flies_time = on ? q2m_now(game) + 60 : DBL_MAX;
    }
  }
  if (entity->frame != state->move->last_frame) entity->frame++;
  game->show(game->ctx, entity);
  c->schedule(c->impl_ctx, c, 0.1, "monster_dead_think");
}
void qa_q2m_flies_on(qa_q2m_context *c) {
  if (wet_corpse(c)) return;
  set_flies(c, true);
  c->schedule(c->impl_ctx, c, 60, "M_FliesOff");
}
void qa_q2m_flies_off(qa_q2m_context *c) {
  set_flies(c, false);
  c->game->cancel(c->game->ctx, c->entity);
}
qa_vec3 qa_q2m_body_of(qa_q2f_game *game, qa_q2f_entity *entity) {
  return entity == NULL ? q2m_zero() : q2m_body(game, entity).origin;
}

