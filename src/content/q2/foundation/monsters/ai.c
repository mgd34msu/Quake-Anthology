/* Port of src/content/q2/foundation/monsters/ai.ts. GPL-2.0-or-later. */
#include "qa/content/q2/foundation/monsters/ai.h"

#include <float.h>
#include <math.h>

#include "monsters_internal.h"
#include "qa/content/q2/foundation/monsters/alternate_fly.h"
#include "qa/content/q2/foundation/monsters/perception.h"
#include "qa/foundation/js.h"

double qa_q2m_monster_solid_mask(qa_q2_game_services *game) {
  return (double)(qa_js_to_int32(qa_js_to_uint32(QA_Q2M_MASK_MONSTERSOLID) | (q2m_rr(game) ? 0x40000000u : 0u)));
}
double qa_q2m_attack_trace_mask(qa_q2_game_services *game) {
  return (double)(qa_js_to_int32(qa_js_to_uint32(QA_Q2M_MASK_SHOT) | (q2m_rr(game) ? 0x40000000u : 0u)));
}
double qa_q2m_health(qa_q2_game_services *game, const qa_actor_id *actor) {
  if (!actor) return 0;
  qa_combat_state_result r = qa_gameplay_authority_read(game->host->combat, *actor);
  return r.has_value ? r.value.health : 0;
}
bool qa_q2m_enemy_body(qa_q2m_context *c, qa_body_state *out) {
  if (!c->entity->has_enemy) return false;
  qa_body_state_result r = qa_shared_body_table_read(c->game->host->bodies, c->entity->enemy);
  if (!r.has_value) return false;
  *out = r.value;
  return true;
}
bool qa_q2m_enemy_eye(qa_q2m_context *c, qa_vec3 *out) {
  qa_body_state body;
  if (!qa_q2m_enemy_body(c, &body)) return false;
  qa_q2_monster_target_result observed = c->game->monster_target(c->game->ctx, &c->entity->enemy);
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
  double dx = qa_js_max(0, qa_js_max(enemy.origin.x + enemy.bounds.min.x - self.origin.x - self.bounds.max.x,
                           self.origin.x + self.bounds.min.x - enemy.origin.x - enemy.bounds.max.x));
  double dy = qa_js_max(0, qa_js_max(enemy.origin.y + enemy.bounds.min.y - self.origin.y - self.bounds.max.y,
                           self.origin.y + self.bounds.min.y - enemy.origin.y - enemy.bounds.max.y));
  double dz = qa_js_max(0, qa_js_max(enemy.origin.z + enemy.bounds.min.z - self.origin.z - self.bounds.max.z,
                           self.origin.z + self.bounds.min.z - enemy.origin.z - enemy.bounds.max.z));
  return qa_js_hypot3(dx, dy, dz);
}
bool qa_q2m_visible(qa_q2m_context *c, const qa_actor_id *actor) {
  if (!actor) return false;
  qa_body_state_result target = qa_shared_body_table_read(c->game->host->bodies, *actor);
  if (!target.has_value) return false;
  qa_vec3 origin = q2m_body(c->game, c->entity).origin;
  qa_vec3 start = origin;
  start.z = origin.z + c->entity->view_height;
  qa_q2_monster_target_result observed = c->game->monster_target(c->game->ctx, actor);
  if (!observed.has_value) return false;
  qa_vec3 end = target.value.origin;
  end.z = target.value.origin.z + observed.value.view_height;
  const bool rerelease = q2m_rr(c->game);
  qa_trace_result trace = q2m_trace(c->game, start, end, NULL, &c->entity->actor.id, QA_Q2M_MASK_OPAQUE);
  if (!(trace.fraction == 1 || ((rerelease || c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE) &&
                                trace.hit.kind == QA_TRACE_HIT_ACTOR && qa_same_actor(trace.hit.actor, *actor))))
    return false;
  if (rerelease && observed.value.has_cloak_alpha)
    return observed.value.cloak_alpha > 0 && q2m_rnd(c->game) <= observed.value.cloak_alpha;
  return true;
}
/* The rerelease M_CheckAttack and ai_checkattack gate: a cloaked player whose reveal fade has ended cannot be attacked. */
bool qa_q2m_enemy_cloaked(qa_q2m_context *c) {
  if (!q2m_rr(c->game) || !c->entity->has_enemy) return false;
  qa_q2_monster_target_result observed = c->game->monster_target(c->game->ctx, &c->entity->enemy);
  return observed.has_value && observed.value.has_cloak_alpha && observed.value.cloak_alpha == 0;
}
bool qa_q2m_visible_enemy(qa_q2m_context *c) {
  return qa_q2m_visible(c, c->entity->has_enemy ? &c->entity->enemy : NULL);
}
bool qa_q2m_in_front(qa_q2m_context *c, qa_actor_id actor) {
  qa_body_state_result body = qa_shared_body_table_read(c->game->host->bodies, actor);
  if (!body.has_value) return false;
  qa_body_state self = q2m_body(c->game, c->entity);
  if (c->state->third_eye && q2m_rr(c->game)) return true;
  const double facing = qa_dot3_double(qa_math_angle_vectors_rule(self.angles, QA_ANGLE_RULE_NATIVE).forward,
                                       qa_normalize3_double_hypot(qa_sub3_double(body.value.origin, self.origin)));
  if (!q2m_rr(c->game)) return facing > 0.3;
  return (qa_js_to_int32(c->entity->spawnflags) & 1) != 0 && c->state->trail_time == 0 && !c->entity->has_enemy
             ? facing > 0.15 : facing > -0.3;
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
  double mask = rr ? (double)(qa_js_to_int32(0x42000003u | 0x4000u)) : QA_Q2M_MASK_SHOT;
  if (clear_to(c, start, blind ? c->state->blind_fire_target : eye, mask, rr)) return true;
  qa_body_state enemy;
  return rr && !blind && qa_q2m_enemy_body(c, &enemy) && clear_to(c, start, enemy.origin, mask, rr);
}
bool qa_q2m_trace_ground_actor(qa_trace_result trace, qa_q2_game_services *game, qa_actor_id *out) {
  if (trace.hit.kind == QA_TRACE_HIT_ACTOR) { *out = trace.hit.actor; return true; }
  if (trace.hit.kind == QA_TRACE_HIT_WORLD) { *out = game->host->world_actor(game->host->ctx); return true; }
  return false;
}
void qa_q2m_change_yaw(qa_q2m_context *c) {
  qa_body_state body = q2m_body(c->game, c->entity);
  double current = fmod(fmod(body.angles.y, 360) + 360, 360);
  double move = c->state->ideal_yaw - current;
  /* m_move.c tests the boundary inclusively on the side the ideal lies, so an exact half turn reverses. */
  if (c->state->ideal_yaw > current) { if (move >= 180) move -= 360; }
  else if (move <= -180) move += 360;
  double speed = c->state->yaw_speed * (q2m_rr(c->game) ? c->game->host->frame_seconds(c->game->host->ctx) * 10 : 1);
  move = qa_js_max(-speed, qa_js_min(speed, move));
  qa_q2_body_changes changes;
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
/* SV_CheckBottom for any entity, so non-monster walkers like barrels share the monster ledge test. */
bool qa_q2m_water_above_waist_at(qa_q2_game_services *game, qa_vec3 point, double mins_z) {
  static const double rises[3] = {0, 26, 48};
  for (size_t i = 0; i < 3; i++) {
    qa_vec3 probe = {point.x, point.y, point.z + mins_z + 1 + rises[i]};
    if ((qa_js_to_uint32(game->host->point_contents(game->host->ctx, probe)) & qa_js_to_uint32(QA_Q2M_MASK_WATER)) == 0) return false;
  }
  return true;
}
bool qa_q2m_check_entity_bottom(qa_q2_game_services *game, qa_q2_entity *entity, qa_vec3 origin) {
  qa_body_state body = q2m_body(game, entity);
  qa_vec3 minimum = qa_add3_double(origin, body.bounds.min), maximum = qa_add3_double(origin, body.bounds.max);
  bool ceiling = entity->gravity_vector.z > 0;
  double direction = ceiling ? 1 : -1;
  double support = ceiling ? maximum.z : minimum.z;
  qa_vec3 corners[4] = {
    q2m_v3(minimum.x, minimum.y, support + direction), q2m_v3(minimum.x, maximum.y, support + direction),
    q2m_v3(maximum.x, minimum.y, support + direction), q2m_v3(maximum.x, maximum.y, support + direction),
  };
  bool all_solid = true;
  for (int i = 0; i < 4; i++) if (game->host->point_contents(game->host->ctx, corners[i]) != 1) all_solid = false;
  if (all_solid) return true;
  bool rr = q2m_rr(game);
  qa_vec3 center = q2m_v3((minimum.x + maximum.x) * 0.5, (minimum.y + maximum.y) * 0.5, support);
  qa_vec3 start = rr ? q2m_v3(origin.x, origin.y, support) : center;
  qa_bounds middle_bounds;
  const qa_bounds *mb = NULL;
  if (rr) {
    middle_bounds = body.bounds; middle_bounds.min.z = 0; middle_bounds.max.z = 0; mb = &middle_bounds;
  }
  qa_vec3 middle_end = start; middle_end.z = start.z + direction * 36;
  qa_trace_result middle = q2m_trace(game, start, middle_end, mb, &entity->actor.id, qa_q2m_monster_solid_mask(game));
  if (middle.fraction == 1) return false;
  if (rr && (qa_js_to_uint32(entity->spawnflags) & 131072u) != 0) return true;
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
    qa_trace_result trace = q2m_trace(game, point, end, cb, &entity->actor.id, qa_q2m_monster_solid_mask(game));
    if (trace.fraction == 1 || (trace.end.z - middle.end.z) * direction > 18) return false;
  }
  return true;
}
bool qa_q2m_check_bottom(qa_q2m_context *c, qa_vec3 origin) { return qa_q2m_check_entity_bottom(c->game, c->entity, origin); }

static double contents_at(qa_q2_game_services *game, qa_vec3 point) {
  return game->host->point_contents(game->host->ctx, point);
}
static bool water_at(qa_q2_game_services *game, qa_vec3 point) {
  return (qa_js_to_uint32(contents_at(game, point)) & qa_js_to_uint32(QA_Q2M_MASK_WATER)) != 0;
}
/* G_Impact for a failed fly move: both the mover and the solid it ran into see the touch. */
static void fly_impact(qa_q2m_context *c, qa_trace_result trace) {
  if (trace.hit.kind != QA_TRACE_HIT_ACTOR) return;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2_entity *other = game->entity(game->ctx, &trace.hit.actor);
  if (other == NULL) return;
  const qa_plane plane = trace.contact.plane;
  if (entity->touch.fn != NULL && strcmp(entity->solid, "none") != 0) {
    qa_touch_contact contact; memset(&contact, 0, sizeof contact);
    contact.self = entity->actor; contact.other = other->actor.id; contact.plane = trace.contact.has_plane ? &plane : NULL;
    entity->touch.fn(entity->touch.ctx, entity, game, contact);
  }
  if (qa_session_actor_registry_is_live(game->host->actors, entity->actor.id) &&
      qa_session_actor_registry_is_live(game->host->actors, other->actor.id) && other->touch.fn != NULL &&
      strcmp(other->solid, "none") != 0) {
    qa_touch_contact contact; memset(&contact, 0, sizeof contact);
    contact.self = other->actor; contact.other = entity->actor.id;
    other->touch.fn(other->touch.ctx, other, game, contact);
  }
}
/* M_CatagorizePosition at the destination: a walker may wade to its waist but refuses deep water, slime and lava. */
static bool refuses_water(qa_q2m_context *c, qa_vec3 origin, double support_offset) {
  qa_q2_game_services *game = c->game;
  qa_bounds bounds = q2m_body(game, c->entity).bounds;
  qa_vec3 point = origin;
  point.z = origin.z + support_offset;
  const uint32_t feet = qa_js_to_uint32(contents_at(game, point));
  if ((feet & qa_js_to_uint32(QA_Q2M_MASK_WATER)) == 0) return false;
  point.z = origin.z + c->entity->view_height;
  const uint32_t eye = qa_js_to_uint32(contents_at(game, point));
  point.z = origin.z + (bounds.min.z + bounds.max.z) * 0.5;
  return (eye & qa_js_to_uint32(QA_Q2M_MASK_WATER)) != 0 || (feet & (8u | 16u)) != 0 ||
         (qa_js_to_uint32(contents_at(game, point)) & (8u | 16u)) != 0;
}
/* SV_movestep's bump handling: after a blocked move, stop direct pursuit for a while and slide the heading along the obstacle. */
static void slide_along_obstacle(qa_q2m_context *c, qa_vec3 displacement, qa_trace_result trace) {
  qa_q2m_state *state = c->state;
  const double now = q2m_now(c->game);
  state->bad_move_time = now + 5;
  if (state->bump_time > now || !trace.contact.has_plane) return;
  state->bump_time = now + 0.5;
  const qa_vec3 normal = trace.contact.plane.normal;
  const double along = qa_dot3_double(displacement, normal);
  const double x = displacement.x - normal.x * along, y = displacement.y - normal.y * along;
  if (qa_js_hypot2(x, y) > 0.01) state->ideal_yaw = qa_vector_to_angles_with_rule(q2m_v3(x, y, 0), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
}
static bool source_move_step(qa_q2m_context *c, double yaw, double distance, bool commit, bool relink) {
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  qa_body_state body = q2m_body(game, entity);
  if (state->locomotion == QA_Q2M_LOCO_STATIONARY) return false;
  if (body.ground == NULL && state->locomotion == QA_Q2M_LOCO_WALK) return false;
  double radians = yaw * 3.141592653589793 / 180;
  qa_q2m_before_move_result step = c->before_source_move(c->impl_ctx, c, q2m_v3(cos(radians) * distance, sin(radians) * distance, 0));
  if (step.handled) return true;
  qa_vec3 destination = qa_add3_double(body.origin, step.displacement);
  const bool rr = q2m_rr(game);
  if (state->locomotion == QA_Q2M_LOCO_FLY || state->locomotion == QA_Q2M_LOCO_SWIM) {
    if (rr && state->alt.alternate_fly && qa_q2m_alternate_fly_step(c)) return true;
    qa_vec3 feet1 = body.origin; feet1.z = body.origin.z + body.bounds.min.z + 1;
    qa_vec3 feet27 = body.origin; feet27.z = body.origin.z + body.bounds.min.z + 27;
    bool wet = water_at(game, feet1);
    bool deep = water_at(game, feet27);
    bool has_blocked = false;
    qa_trace_result blocked; memset(&blocked, 0, sizeof blocked);
    for (int attempt = 0; attempt < 2; attempt++) {
      qa_vec3 end = destination;
      if (attempt == 0 && entity->has_enemy) {
        if (!entity->has_goal) { entity->goal = entity->enemy; entity->has_goal = true; }
        qa_body_state_result goal = qa_shared_body_table_read(game->host->bodies, entity->goal);
        if (goal.has_value) {
          double dz = body.origin.z - goal.value.origin.z;
          double stride = rr ? qa_js_hypot3(step.displacement.x, step.displacement.y, step.displacement.z) : 8;
          double min_height = strcmp(entity->classname, "monster_carrier") == 0 ? 104 : 40;
          if (rr && strcmp(entity->classname, "monster_fixbot") == 0) {
            const double frame = entity->frame;
            const double nudge = frame >= 105 && frame <= 120 ? 1 : frame >= 31 && frame <= 88 ? 2 : 4;
            end.z += dz > 12 ? -nudge : dz < -12 ? nudge : 0;
          } else if (game->host->is_player(game->host->ctx, entity->goal)) {
            const double hx = rr ? end.x - step.displacement.x * 0.5 : end.x;
            const double hy = rr ? end.y - step.displacement.y * 0.5 : end.y;
            if (dz > min_height) end = q2m_v3(hx, hy, end.z - stride);
            if (dz < min_height - 10 && !(state->locomotion == QA_Q2M_LOCO_SWIM && !deep)) end = q2m_v3(hx, hy, end.z + stride);
          } else end.z += dz > stride ? -stride : dz > 0 ? -dz : dz < -stride ? stride : dz;
        }
      }
      qa_trace_result trace = q2m_trace(game, body.origin, end, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game));
      qa_vec3 enter = trace.end; enter.z = trace.end.z + body.bounds.min.z + 1;
      bool enters_water = water_at(game, enter);
      if ((state->locomotion == QA_Q2M_LOCO_FLY && !wet && enters_water)
          || (state->locomotion == QA_Q2M_LOCO_SWIM && !deep && !enters_water)) return false;
      if (trace.fraction == 1 && !trace.start_solid && !trace.all_solid) {
        if (commit) {
          qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
          changes.has_origin = true; changes.origin = trace.end;
          q2m_move(game, entity, changes, true, relink);
          if (relink) game->host->touch_triggers(game->host->ctx, entity->actor);
        }
        return true;
      }
      blocked = trace; has_blocked = true;
      if (!entity->has_enemy) break;
    }
    if (commit && has_blocked && rr) fly_impact(c, blocked);
    return false;
  }
  qa_vec3 gravity = entity->gravity_vector;
  bool ceiling = gravity.z > 0;
  const double step_size = rr ? (state->no_step ? 1 : (qa_js_to_uint32(entity->spawnflags) & 131072u) != 0 ? 64 : 18) : 18;
  const double mask = qa_q2m_monster_solid_mask(game);
  qa_vec3 reach = destination;
  if (rr) {
    const qa_vec3 lift = qa_scale3_double(gravity, -step_size);
    qa_trace_result raised = q2m_trace(game, qa_add3_double(body.origin, lift), qa_add3_double(destination, lift), &body.bounds,
                                       &entity->actor.id, mask);
    qa_trace_result flat = q2m_trace(game, body.origin, destination, &body.bounds, &entity->actor.id, mask);
    const qa_trace_result *forward = NULL;
    if (!raised.start_solid && !raised.all_solid) forward = &raised;
    if (!flat.start_solid && !flat.all_solid && (forward == NULL || flat.fraction > forward->fraction)) forward = &flat;
    if (forward == NULL) return false;
    reach = q2m_v3(forward->end.x, forward->end.y, destination.z);
    if (forward->fraction < 1 && qa_js_hypot2(reach.x - body.origin.x, reach.y - body.origin.y) < distance * 0.25) {
      if (commit) slide_along_obstacle(c, step.displacement, *forward);
      return true;
    }
  }
  qa_vec3 start = qa_add3_double(reach, qa_scale3_double(gravity, -step_size));
  qa_vec3 end = qa_add3_double(start, qa_scale3_double(gravity, step_size * 2));
  qa_trace_result trace = q2m_trace(game, start, end, &body.bounds, &entity->actor.id, mask);
  if (trace.all_solid) return false;
  if (trace.start_solid) {
    qa_vec3 retry = start;
    if (!rr) retry.z = start.z - 18; else retry = reach;
    trace = q2m_trace(game, retry, end, &body.bounds, &entity->actor.id, mask);
    if (trace.start_solid || trace.all_solid) return false;
  }
  double support_offset = ceiling ? body.bounds.max.z - 1 : body.bounds.min.z + 1;
  qa_vec3 feet = body.origin; feet.z = body.origin.z + support_offset;
  const bool dry = !water_at(game, feet);
  if (dry) {
    qa_vec3 end_feet = trace.end; end_feet.z = trace.end.z + support_offset;
    if (rr ? refuses_water(c, trace.end, support_offset) : water_at(game, end_feet)) return false;
  }
  if (trace.fraction == 1) {
    if ((qa_js_to_uint32(entity->flags) & 256u) == 0 && !(rr && state->dead)) return false;
    if (commit) {
      qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_origin = true; changes.origin = reach;
      changes.has_ground = true; changes.ground_null = true;
      q2m_move(game, entity, changes, true, relink);
      if (relink) game->host->touch_triggers(game->host->ctx, entity->actor);
    }
    return true;
  }
  if (rr && trace.contact.has_plane && trace.contact.plane.normal.z * (ceiling ? -1 : 1) < 0.7) return false;
  if (!c->accepts_source_ground_move(c->impl_ctx, c, trace.end)) return false;
  if (!qa_q2m_check_bottom(c, trace.end)) {
    if ((qa_js_to_uint32(entity->flags) & 256u) == 0) return false;
    if (commit) {
      qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_origin = true; changes.origin = trace.end;
      q2m_move(game, entity, changes, true, relink);
      if (relink) game->host->touch_triggers(game->host->ctx, entity->actor);
    }
    return true;
  }
  if (commit) {
    entity->flags = (double)(qa_js_to_int32(qa_js_to_uint32(entity->flags) & ~256u));
    qa_actor_id ground;
    bool has_ground = qa_q2m_trace_ground_actor(trace, game, &ground);
    qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_origin = true; changes.origin = trace.end;
    changes.has_ground = true; changes.ground_null = !has_ground;
    if (has_ground) changes.ground = ground;
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
  return qa_q2m_step_direction_turns(c, yaw, distance, true);
}
bool qa_q2m_step_direction_turns(qa_q2m_context *c, double yaw, double distance, bool allow_no_turns) {
  bool rerelease = q2m_rr(c->game);
  double old_ideal_yaw = c->state->ideal_yaw, old_yaw = q2m_body(c->game, c->entity).angles.y;
  c->state->ideal_yaw = yaw;
  qa_q2m_change_yaw(c);
  qa_body_state previous = q2m_body(c->game, c->entity);
  if (!qa_q2m_walk_move(c, yaw, distance, true, false)) {
    if (rerelease) {
      c->state->ideal_yaw = old_ideal_yaw;
      qa_q2_body_changes restore; memset(&restore, 0, sizeof restore);
      restore.has_angles = true; restore.angles = q2m_body(c->game, c->entity).angles; restore.angles.y = old_yaw;
      q2m_move(c->game, c->entity, restore, true, false);
    }
    c->game->link(c->game->ctx, c->entity);
    c->game->host->touch_triggers(c->game->host->ctx, c->entity->actor);
    return !q2m_live(c->game, c->entity->actor.id);
  }
  (void)c->consume_source_blocked(c->impl_ctx, c);
  if (!q2m_live(c->game, c->entity->actor.id)) return true;
  /* SV_StepDirection compares the raw yaw difference in the classic game; the rerelease wraps it. */
  double raw_delta = q2m_body(c->game, c->entity).angles.y - c->state->ideal_yaw;
  double delta = !rerelease ? raw_delta : fmod(fmod(raw_delta, 360) + 360, 360);
  bool result = true;
  if (delta > 45 && delta < 315
      && !(c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE
           && strncmp(c->entity->classname, "monster_widow", 13) == 0)) {
    qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
    changes.has_origin = true; changes.origin = previous.origin;
    q2m_move(c->game, c->entity, changes, true, false);
    if (rerelease) result = allow_no_turns;
  }
  c->game->link(c->game->ctx, c->entity);
  c->game->host->touch_triggers(c->game->host->ctx, c->entity->actor);
  return result;
}
bool qa_q2m_chase_direction(qa_q2m_context *c, qa_vec3 goal, double distance) {
  double old = trunc(c->state->ideal_yaw / 45) * 45;
  double turnaround = fmod(old - 180 + 360, 360);
  qa_vec3 delta = qa_sub3_double(goal, q2m_body(c->game, c->entity).origin);
  double x = delta.x > 10 ? 0 : delta.x < -10 ? 180 : -1;
  double y = delta.y < -10 ? 270 : delta.y > 10 ? 90 : -1;
  if (x != -1 && y != -1) {
    double diagonal = x == 0 ? (y == 90 ? 45 : 315) : (y == 90 ? 135 : 215);
    if (diagonal != turnaround && qa_q2m_step_direction_turns(c, diagonal, distance, !q2m_rr(c->game))) return true;
  }
  double direction_roll = floor(q2m_rnd(c->game) * 4);
  bool rogue = c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE;
  if ((qa_js_to_int32(direction_roll) & 1) != 0 || fabs(delta.y) > fabs(delta.x)) {
    double tmp = x; x = y; y = tmp;
  }
  if (x != -1 && x != turnaround && qa_q2m_step_direction_turns(c, x, distance, !q2m_rr(c->game))) return true;
  if (y != -1 && y != turnaround && qa_q2m_step_direction_turns(c, y, distance, !q2m_rr(c->game))) return true;
  if (rogue && q2m_live(c->game, c->entity->actor.id)
      && qa_q2m_health(c->game, &c->entity->actor.id) > 0 && c->blocked(c->impl_ctx, c, distance)) return true;
  if (old != -1 && qa_q2m_step_direction_turns(c, old, distance, !q2m_rr(c->game))) return true;
  bool descending = floor(q2m_rnd(c->game) * 2) == 0;
  for (int index = 0; index < 8; index++) {
    double yaw = descending ? 315 - index * 45 : index * 45;
    if (yaw != turnaround && qa_q2m_step_direction_turns(c, yaw, distance, !q2m_rr(c->game))) return true;
  }
  if (turnaround != -1 && qa_q2m_step_direction_turns(c, turnaround, distance, !q2m_rr(c->game))) return true;
  c->state->ideal_yaw = q2m_rr(c->game) ? q2m_rnd(c->game) * 360 : old;
  if (!qa_q2m_check_bottom(c, q2m_body(c->game, c->entity).origin))
    c->entity->flags = (double)(qa_js_to_int32(qa_js_to_uint32(c->entity->flags) | 256u));
  return false;
}

static void slide(qa_q2m_context *c, double distance) {
  qa_q2m_state *state = c->state;
  qa_q2_game_services *game = c->game;
  if (!state->manual_steering) qa_q2m_face_enemy(c);
  double side = state->lefty ? 90 : -90;
  /* Rogue's `!self->flags & FL_FLY` clamps only a monster whose flags are all clear. */
  double amount = q2m_rr(game) && state->locomotion != QA_Q2M_LOCO_FLY
    ? qa_js_min(distance, 8 / (game->host->frame_seconds(game->host->ctx) * 100))
    : c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE && c->entity->flags == 0 ? qa_js_min(distance, 8) : distance;
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
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  qa_q2m_state *state = c->state;
  bool rogue = c->source_combat_rules(c->impl_ctx, c) == QA_Q2M_RULES_ROGUE;
  bool rr = q2m_rr(game);
  bool extended = rr || rogue;
  if (ai == QA_Q2M_AI_NONE || ai == QA_Q2M_AI_SOURCE) return;
  if (ai == QA_Q2M_AI_TURN) {
    if (distance != 0 || state->alt.alternate_fly) (void)qa_q2m_walk_move(c, q2m_body(game, entity).angles.y, distance, true, true);
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
    if (distance != 0 || state->alt.alternate_fly) {
      if (extended && state->charging) { (void)c->move_to_goal(c->impl_ctx, c, distance); return; }
      double yaw = q2m_body(game, entity).angles.y;
      if (extended && state->attack_state == QA_Q2M_ATTACK_SLIDING) {
        qa_q2_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
        const char *enemy_class = enemy_entity ? enemy_entity->classname : "";
        double side = (rogue && !rr && strcmp(enemy_class, "tesla") == 0) || (rr && strcmp(enemy_class, "tesla_mine") == 0) ? 0
                      : state->lefty ? 90 : -90;
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
    if (distance != 0 || state->alt.alternate_fly) (void)qa_q2m_walk_move(c, q2m_body(game, entity).angles.y, distance, true, true);
    if (!q2m_live(game, entity->actor.id)) return;
    if (state->stand_ground) {
      if (rr && state->reached_hold_combat && state->has_move_target) {
        qa_body_state_result point = qa_shared_body_table_read(game->host->bodies, state->move_target);
        qa_body_state self = q2m_body(game, entity);
        bool overlaps = point.has_value;
        for (int axis = 0; overlaps && axis < 3; axis++) {
          const double po = axis == 0 ? point.value.origin.x : axis == 1 ? point.value.origin.y : point.value.origin.z;
          const double pmin = axis == 0 ? point.value.bounds.min.x : axis == 1 ? point.value.bounds.min.y : point.value.bounds.min.z;
          const double pmax = axis == 0 ? point.value.bounds.max.x : axis == 1 ? point.value.bounds.max.y : point.value.bounds.max.z;
          const double so = axis == 0 ? self.origin.x : axis == 1 ? self.origin.y : self.origin.z;
          const double smin = axis == 0 ? self.bounds.min.x : axis == 1 ? self.bounds.min.y : self.bounds.min.z;
          const double smax = axis == 0 ? self.bounds.max.x : axis == 1 ? self.bounds.max.y : self.bounds.max.z;
          overlaps = po + pmin <= so + smax && po + pmax >= so + smin;
        }
        if (!overlaps) {
          state->stand_ground = false;
          state->reached_hold_combat = false;
          state->third_eye = false;
          state->combat_point = true;
          entity->goal = state->move_target; entity->has_goal = true;
          c->run(c->impl_ctx, c);
          return;
        }
      }
      qa_q2_entity *enemy_entity = entity->has_enemy && rr ? game->entity(game->ctx, &entity->enemy) : NULL;
      if (entity->has_enemy && !(rr && enemy_entity != NULL && strcmp(enemy_entity->classname, "player_noise") == 0)) {
        qa_body_state enemy;
        bool has_enemy = qa_q2m_enemy_body(c, &enemy);
        if (has_enemy) state->ideal_yaw = qa_vector_to_angles_with_rule(
            qa_sub3_double(enemy.origin, q2m_body(game, entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
        if ((rr ? !qa_q2m_facing_ideal(c) : q2m_body(game, entity).angles.y != state->ideal_yaw) && state->temporary_stand_ground) {
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
    if (rr && entity->has_enemy && !state->has_sound_target) {
      qa_body_state enemy;
      const bool has_enemy = qa_q2m_enemy_body(c, &enemy);
      entity->goal = entity->enemy; entity->has_goal = true;
      c->run(c->impl_ctx, c);
      if (has_enemy) state->ideal_yaw = qa_vector_to_angles_with_rule(
          qa_sub3_double(enemy.origin, q2m_body(game, entity).origin), QA_VECTOR_TO_ANGLES_NATIVE_AI).y;
      return;
    }
    if (c->find_target(c->impl_ctx, c)) return;
    if (q2m_now(game) > state->pause_time) { c->walk(c->impl_ctx, c); return; }
    if (state->has_idle && (qa_js_to_uint32(entity->spawnflags) & 1u) == 0 && q2m_now(game) > state->idle_time) {
      double left4;
      double right4;
      if (state->idle_time != 0) { c->idle(c->impl_ctx, c); double left2 = q2m_now(game) + 15; double right2 = q2m_rnd(game) * 15; state->idle_time = left2 + right2; }
      else state->idle_time = (left4 = q2m_now(game), right4 = q2m_rnd(game) * 15, left4 + right4);
    }
    return;
  }
  if (ai == QA_Q2M_AI_WALK) {
    (void)c->move_to_goal(c->impl_ctx, c, distance);
    if (!q2m_live(game, entity->actor.id)) return;
    if (c->find_target(c->impl_ctx, c)) return;
    if (state->has_search && q2m_now(game) > state->idle_time) {
      double left3;
      double right3;
      if (state->idle_time != 0) { c->search(c->impl_ctx, c); double left = q2m_now(game) + 15; double right = q2m_rnd(game) * 15; state->idle_time = left + right; }
      else state->idle_time = (left3 = q2m_now(game), right3 = q2m_rnd(game) * 15, left3 + right3);
    }
    return;
  }
  /* "run" */
  if (state->combat_point) {
    if (rr) {
      if (c->check_attack(c->impl_ctx, c, distance)) return;
      if (state->reached_hold_combat) {
        qa_body_state_result point = state->has_move_target ? qa_shared_body_table_read(game->host->bodies, state->move_target)
                                                            : (qa_body_state_result){0};
        qa_body_state self = q2m_body(game, entity);
        const bool away = !point.has_value || qa_hypot3(qa_sub3_double(point.value.origin, self.origin)) > 160 ||
                          q2m_trace(game, self.origin, point.value.origin, NULL, &entity->actor.id, qa_q2m_monster_solid_mask(game)).fraction < 1;
        if (away) {
          state->reached_hold_combat = false;
          state->third_eye = false;
          state->combat_point = false;
          entity->has_goal = entity->has_enemy;
          if (entity->has_enemy) entity->goal = entity->enemy;
          return;
        }
      }
    }
    (void)c->move_to_goal(c->impl_ctx, c, distance);
    return;
  }
  if (rogue && !rr) {
    state->ducked = false;
    qa_body_state body = q2m_body(game, entity);
    if (body.bounds.max.z != state->normal_height) {
      state->can_take_damage = true; state->next_duck_time = q2m_now(game) + 0.5;
      qa_combat_trait_changes traits; memset(&traits, 0, sizeof traits);
      traits.has_can_take_damage = true; traits.can_take_damage = true;
      qa_gameplay_authority_set_traits(game->host->combat, entity->actor, traits);
      qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
      changes.has_bounds = true; changes.bounds = body.bounds; changes.bounds.max.z = state->normal_height;
      q2m_move(game, entity, changes, false, false);
    }
  }
  if (c->run_hint_path(c->impl_ctx, c, distance)) return;
  bool already_moved = false;
  if (state->has_sound_target) {
    qa_vec3 delta = qa_sub3_double(q2m_body(game, entity).origin, state->sound_target.origin);
    bool arrived;
    if (rr) {
      qa_body_state_result noise = qa_shared_body_table_read(game->host->bodies, state->sound_target.actor);
      qa_body_state self = q2m_body(game, entity);
      arrived = noise.has_value && qa_q2m_facing_ideal(c);
      for (int axis = 0; arrived && axis < 3; axis++) {
        const double no = axis == 0 ? noise.value.origin.x : axis == 1 ? noise.value.origin.y : noise.value.origin.z;
        const double nmin = axis == 0 ? noise.value.bounds.min.x : axis == 1 ? noise.value.bounds.min.y : noise.value.bounds.min.z;
        const double nmax = axis == 0 ? noise.value.bounds.max.x : axis == 1 ? noise.value.bounds.max.y : noise.value.bounds.max.z;
        const double so = axis == 0 ? self.origin.x : axis == 1 ? self.origin.y : self.origin.z;
        const double smin = axis == 0 ? self.bounds.min.x : axis == 1 ? self.bounds.min.y : self.bounds.min.z;
        const double smax = axis == 0 ? self.bounds.max.x : axis == 1 ? self.bounds.max.y : self.bounds.max.z;
        arrived = no + nmin <= so + smax + distance && no + nmax >= so + smin - distance;
      }
    } else arrived = qa_hypot3(delta) < 64;
    if (arrived) {
      state->stand_ground = true; state->temporary_stand_ground = true; state->close_sight_tripped = false;
      c->stand(c->impl_ctx, c); return;
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
    if ((distance != 0 || state->alt.alternate_fly) && !already_moved && state->attack_state == QA_Q2M_ATTACK_STRAIGHT && !state->stand_ground)
      (void)c->move_to_goal(c->impl_ctx, c, distance);
    return;
  }
  if (state->stand_ground) return;
  if (rr && entity->has_enemy && qa_q2m_target_distance(c) <= 50) qa_q2m_change_yaw(c);
  if (!qa_q2m_visible_enemy(c) && c->check_lost_hint_path(c->impl_ctx, c)) return;
  if (!qa_q2m_visible_enemy(c) && game->options.mode == QA_Q2_MODE_COOP && c->find_target(c->impl_ctx, c)) return;
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
  qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
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
  return (qa_js_to_uint32(c->game->host->point_contents(c->game->host->ctx, point)) & qa_js_to_uint32(QA_Q2M_MASK_WATER)) != 0;
}
void qa_q2m_fly_check(qa_q2m_context *c, bool retry) {
  if (wet_corpse(c) || q2m_rnd(c->game) > 0.5) {
    if (retry) c->schedule(c->impl_ctx, c, 0.1, "M_FlyCheck");
    return;
  }
  c->schedule(c->impl_ctx, c, 5 + 10 * q2m_rnd(c->game), "M_FliesOn");
}
void qa_q2m_corpse(qa_q2m_context *c) {
  c->state->corpse = true;
  c->entity->server_flags = (double)(qa_js_to_int32(qa_js_to_uint32(c->entity->server_flags) | 2u));
  qa_q2_body_changes changes; memset(&changes, 0, sizeof changes);
  changes.has_bounds = true;
  changes.bounds = (qa_bounds){q2m_v3(-16, -16, -24), q2m_v3(16, 16, -8)};
  q2m_move(c->game, c->entity, changes, false, false);
  c->game->motion(c->game->ctx, c->entity, "toss");
  c->game->cancel(c->game->ctx, c->entity);
  if (!q2m_rr(c->game)) {
    if (strcmp(c->state->kind, "infantry") == 0) qa_q2m_fly_check(c, true);
  } else c->schedule(c->impl_ctx, c, 0.1, "monster_dead_think");
}
static void set_flies(qa_q2m_context *c, bool on) {
  if (on) c->entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(c->entity->effects) | 0x4000u));
  else c->entity->effects = (double)(qa_js_to_int32(qa_js_to_uint32(c->entity->effects) & ~0x4000u));
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "sound"; e.has_actor = true; e.actor = c->entity->actor.id;
  e.origin = q2m_body(c->game, c->entity).origin; e.path = "infantry/inflies1.wav";
  e.channel = 0; e.volume = 1; e.attenuation = 1; e.loop = on ? "start" : "stop";
  c->game->host->emit(c->game->host->ctx, e);
  c->game->show(c->game->ctx, c->entity);
}
void qa_q2m_monster_dead_think(qa_q2m_context *c) {
  qa_q2m_state *state = c->state;
  qa_q2_game_services *game = c->game;
  qa_q2_entity *entity = c->entity;
  if (strcmp(state->kind, "infantry") == 0) {
    if (!state->has_flies_time) { state->has_flies_time = true; double left = q2m_now(game) + 5; double right = q2m_rnd(game) * 10; state->flies_time = left + right; }
    else if (state->flies_time < q2m_now(game)) {
      bool on = (qa_js_to_uint32(entity->effects) & 0x4000u) == 0;
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
qa_vec3 qa_q2m_body_of(qa_q2_game_services *game, qa_q2_entity *entity) {
  return entity == NULL ? q2m_zero() : q2m_body(game, entity).origin;
}

