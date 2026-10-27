/* Port of src/content/q2/missionpacks/monsters/rogue-common.ts,
 * plus mirrored rerelease/monsters/common.ts helpers (see header).
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"

#include <stdlib.h>

#include "mp_internal.h"
#include "qa/content/q2/base/monsters/parasite.h"
#include "qa/content/q2/foundation/monsters/ai.h"

qa_q2m_named_handler *qa_q2mpm_merge_callbacks(const qa_q2m_named_handler *base, size_t base_count,
                                               const qa_q2m_named_handler *extra, size_t extra_count,
                                               size_t *out_count) {
  size_t total = base_count + extra_count;
  qa_q2m_named_handler *merged = malloc((total ? total : 1) * sizeof *merged);
  if (!merged) qa_q2f_error("Out of memory merging mission-pack monster callbacks");
  size_t count = 0;
  for (size_t i = 0; i < base_count; i++) {
    bool overridden = false;
    for (size_t j = 0; j < extra_count; j++)
      if (strcmp(base[i].name, extra[j].name) == 0) overridden = true;
    if (!overridden) merged[count++] = base[i];
  }
  for (size_t j = 0; j < extra_count; j++) merged[count++] = extra[j];
  *out_count = count;
  return merged;
}
const qa_q2m_handler *qa_q2mpm_find_callback(const qa_q2m_named_handler *table, size_t count, const char *name) {
  for (size_t i = 0; i < count; i++)
    if (strcmp(table[i].name, name) == 0) return &table[i].handler;
  return NULL;
}

bool qa_q2mpm_source_trace_world(qa_q2f_game *game, qa_trace_result trace) {
  return trace.hit.kind == QA_TRACE_HIT_NONE || trace.hit.kind == QA_TRACE_HIT_WORLD
      || qa_same_actor(trace.hit.actor, game->host->world_actor(game->host->ctx));
}
double qa_q2mpm_monster_mass(qa_q2m_context *context) {
  qa_combat_state_result combat = context->game->host->combat.read(context->game->host->combat.ctx,
                                                                    context->entity->actor.id);
  if (!combat.has_value) qa_q2f_error("Source monster has no shared combat state");
  return combat.value.mass;
}
bool qa_q2mpm_rogue_parasite_drain_trace(qa_q2m_context *context, qa_trace_result *out) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(context, &enemy)) return false;
  qa_vec3 start = qa_q2m_project_flash_default(context, q2mpm_v3(24, 0, 6));
  qa_vec3 top = enemy.origin; top.z += enemy.bounds.max.z - 8;
  qa_vec3 bottom = enemy.origin; bottom.z += enemy.bounds.min.z + 8;
  if (!qa_q2bm_parasite_drain_reachable(start, enemy.origin) && !qa_q2bm_parasite_drain_reachable(start, top)
      && !qa_q2bm_parasite_drain_reachable(start, bottom)) return false;
  *out = q2mpm_trace(context->game, start, enemy.origin, NULL, &context->entity->actor.id, 0x6000003);
  return true;
}
bool qa_q2mpm_rogue_blocked_check_shot(qa_q2m_context *context, double chance, qa_q2mpm_monster_state *source) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  if (!entity->has_enemy || !game->host->is_player(game->host->ctx, entity->enemy) || q2mpm_rnd(game) < chance)
    return false;
  if (strcmp(entity->classname, "monster_parasite") == 0) {
    qa_trace_result trace;
    if (!qa_q2mpm_rogue_parasite_drain_trace(context, &trace)) return false;
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !qa_same_actor(trace.hit.actor, entity->enemy)) {
      qa_q2mpm_state_get(source, entity)->blocked = true;
      context->attack(context->impl_ctx, context);
      qa_q2mpm_state_get(source, entity)->blocked = false;
      return true;
    }
  }
  if (!qa_q2m_visible_enemy(context)) return false;
  qa_q2f_entity *enemy = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  const char *tesla = q2mpm_rr(game) ? "tesla_mine" : "tesla";
  if (!enemy || strcmp(enemy->classname, tesla) != 0) return false;
  qa_q2mpm_state_get(source, entity)->blocked = true;
  context->attack(context->impl_ctx, context);
  qa_q2mpm_state_get(source, entity)->blocked = false;
  return true;
}
void qa_q2mpm_rogue_duck_down(qa_q2m_context *context) {
  qa_q2m_state *state = context->state;
  qa_body_state body = q2mpm_body(context->game, context->entity);
  state->ducked = true;
  state->can_take_damage = true;
  q2mpm_set_damageable(context->game, context->entity, true);
  if (state->duck_wait < q2mpm_now(context->game)) state->duck_wait = q2mpm_now(context->game) + 1;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds = body.bounds;
  change.bounds.max.z = state->normal_height - 32;
  q2mpm_move_body(context->game, context->entity, change, false, false);
}
void qa_q2mpm_rogue_duck_hold(qa_q2m_context *context) {
  context->state->hold_frame = q2mpm_now(context->game) < context->state->duck_wait;
}
void qa_q2mpm_rogue_duck_up(qa_q2m_context *context) {
  qa_q2m_state *state = context->state;
  qa_body_state body = q2mpm_body(context->game, context->entity);
  state->ducked = false;
  state->can_take_damage = true;
  state->next_duck_time = q2mpm_now(context->game) + 0.5;
  q2mpm_set_damageable(context->game, context->entity, true);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_bounds = true;
  change.bounds = body.bounds;
  change.bounds.max.z = state->normal_height;
  q2mpm_move_body(context->game, context->entity, change, false, false);
}
void qa_q2mpm_rogue_duck_down_handler(void *ctx, qa_q2m_context *context) { (void)ctx; qa_q2mpm_rogue_duck_down(context); }
void qa_q2mpm_rogue_duck_hold_handler(void *ctx, qa_q2m_context *context) { (void)ctx; qa_q2mpm_rogue_duck_hold(context); }
void qa_q2mpm_rogue_duck_up_handler(void *ctx, qa_q2m_context *context) { (void)ctx; qa_q2mpm_rogue_duck_up(context); }

void qa_q2mpm_rogue_monster_dodge(qa_q2m_context *context, qa_q2m_monsters *monsters, qa_actor_id attacker,
                                  double eta, const qa_trace_result *trace, qa_q2mpm_duck_fn duck,
                                  qa_q2m_handler sidestep) {
  qa_q2f_entity *entity = context->entity;
  qa_q2f_game *game = context->game;
  qa_q2m_state *state = context->state;
  double random = q2mpm_rnd(game);
  if (qa_q2m_health(game, &entity->actor.id) < 1 || (duck.fn == NULL && (sidestep.fn == NULL || state->stand_ground)))
    return;
  if (!entity->has_enemy) {
    entity->has_enemy = true;
    entity->enemy = attacker;
    qa_q2m_monsters_found_target(monsters, context);
  }
  if (eta < 0.1 || eta > 5 || random > 0.25 * (game->options.skill + 1) || trace == NULL) return;
  qa_body_state body = q2mpm_body(game, entity);
  double height = body.origin.z + body.bounds.max.z - (duck.fn == NULL ? 0 : 33);
  bool dodger = sidestep.fn != NULL && !state->stand_ground;
  if (duck.fn != NULL && !dodger && (trace->end.z <= height || state->ducked)) return;
  if (dodger) {
    if (state->dodging) return;
    if (trace->end.z <= height || state->ducked) {
      state->lefty = qa_dot3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).right,
                                    qa_sub3_double(trace->end, body.origin)) >= 0;
      if (duck.fn != NULL && state->ducked) qa_q2mpm_rogue_duck_up(context);
      state->dodging = true;
      state->attack_state = QA_Q2M_ATTACK_SLIDING;
      sidestep.fn(sidestep.ctx, context);
      return;
    }
  }
  if (duck.fn != NULL && state->next_duck_time <= q2mpm_now(game)) {
    qa_q2m_finish_dodge(context);
    state->ducked = true;
    duck.fn(duck.ctx, context, eta);
  }
}

/* ---- mirrored rerelease/monsters/common.ts ---- */
void qa_q2mpm_monster_flash(qa_q2m_context *context, double flash, qa_vec3 origin, qa_vec3 direction) {
  qa_q2f_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = QA_Q2F_EVENT_MONSTER_MUZZLEFLASH;
  e.actor = &context->entity->actor.id;
  e.flash = flash;
  e.origin = origin;
  e.direction = direction;
  context->game->host->emit(context->game->host->ctx, e);
}
static double trace_contents(qa_trace_result trace) { return strcmp(trace.kind, "q1") == 0 ? 0 : trace.contents; }
bool qa_q2mpm_predict_aim(qa_q2m_context *context, qa_vec3 start, double speed, bool eye, double offset,
                          qa_q2mpm_predicted_aim *out) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(context, &enemy)) return false;
  qa_q2f_game *game = context->game;
  qa_q2f_entity *entity = context->entity;
  bool rerelease = q2mpm_rr(game);
  qa_q2f_entity *enemy_entity = entity->has_enemy ? game->entity(game->ctx, &entity->enemy) : NULL;
  double view_height = enemy_entity ? enemy_entity->view_height : 22;
  qa_vec3 aim = enemy.origin;
  aim.z += eye ? view_height : 0;
  qa_vec3 direction = qa_sub3_double(aim, start);
  if (rerelease) {
    qa_trace_result trace = q2mpm_trace(game, start, qa_add3_double(start, direction), NULL, &entity->actor.id,
                                        qa_q2m_attack_trace_mask(game));
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !entity->has_enemy || !qa_same_actor(trace.hit.actor, entity->enemy)) {
      eye = !eye;
      aim = enemy.origin;
      aim.z += eye ? view_height : 0;
      direction = qa_sub3_double(aim, start);
    }
  }
  double time = rerelease && speed == 0 ? 0 : qa_hypot3(direction) / speed;
  qa_vec3 target = qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, time - offset));
  if (rerelease && (qa_dot3_double(qa_normalize3_double_hypot(direction),
                                   qa_normalize3_double_hypot(qa_sub3_double(target, start))) < 0
                    || q2mpm_trace(game, start, target, NULL, NULL, 3).fraction < 0.9))
    target = enemy.origin;
  qa_vec3 point = target;
  point.z += eye ? view_height : 0;
  out->direction = qa_normalize3_double_hypot(qa_sub3_double(point, start));
  out->point = point;
  return true;
}
bool qa_q2mpm_predicted_direction(qa_q2m_context *context, qa_vec3 start, double speed, bool eye, double offset,
                                  qa_vec3 *out) {
  qa_q2mpm_predicted_aim aim;
  if (!qa_q2mpm_predict_aim(context, start, speed, eye, offset, &aim)) return false;
  *out = aim.direction;
  return true;
}
bool qa_q2mpm_blocked_check_platform(qa_q2m_context *context, double distance) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(context, &enemy)) return false;
  qa_q2f_game *game = context->game;
  qa_q2f_entity *entity = context->entity;
  qa_body_state body = q2mpm_body(game, entity);
  bool above = enemy.origin.z + enemy.bounds.min.z >= body.origin.z + body.bounds.max.z;
  bool below = enemy.origin.z + enemy.bounds.max.z <= body.origin.z + body.bounds.min.z;
  if (!above && !below) return false;
  qa_q2f_entity *platform = game->entity(game->ctx, body.ground);
  if (!platform || strncmp(platform->classname, "func_plat", 9) != 0) {
    qa_vec3 start = qa_add3_double(body.origin,
                                   qa_scale3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward, distance));
    qa_vec3 end = start;
    end.z += -384;
    qa_trace_result trace = q2mpm_trace(game, start, end, NULL, &entity->actor.id, qa_q2m_monster_solid_mask(game));
    platform = trace.fraction < 1 && !trace.all_solid && !trace.start_solid && trace.hit.kind == QA_TRACE_HIT_ACTOR
        ? game->entity(game->ctx, &trace.hit.actor) : NULL;
  }
  if (!platform || strncmp(platform->classname, "func_plat", 9) != 0 || platform->use.fn == NULL) return false;
  const char *state = context->platform_state(context->impl_ctx, context, platform->actor.id);
  bool standing = body.ground != NULL && qa_same_actor(*body.ground, platform->actor.id);
  bool use_it = above ? ((standing && state && strcmp(state, "bottom") == 0) || (!standing && state && strcmp(state, "top") == 0))
                      : ((standing && state && strcmp(state, "top") == 0) || (!standing && state && strcmp(state, "bottom") == 0));
  if (use_it) {
    platform->use.fn(platform->use.ctx, platform, game, &entity->actor.id, &entity->actor.id);
    return true;
  }
  return false;
}
bool qa_q2mpm_monster_jump_finished(qa_q2m_context *context) {
  qa_q2f_game *game = context->game;
  qa_q2f_entity *entity = context->entity;
  if (!q2mpm_rr(game)) return q2mpm_now(game) - entity->timestamp > 3;
  qa_body_state body = q2mpm_body(game, entity);
  qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_vec3 scaled = {body.velocity.x * forward.x, body.velocity.y * forward.y, body.velocity.z * forward.z};
  if (qa_hypot3(scaled) < 150) {
    qa_q2f_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_velocity = true;
    change.velocity = qa_scale3_double(forward, 150);
    change.velocity.z = body.velocity.z;
    q2mpm_move_body(game, entity, change, false, false);
  }
  return context->state->jump_time < q2mpm_now(game);
}
qa_q2mpm_jump_result qa_q2mpm_blocked_check_jump(qa_q2m_context *context, double distance, double drop_height,
                                                double jump_height, bool can_jump, qa_q2mpm_jump_navigation navigation) {
  (void)distance;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(context, &enemy)) return QA_Q2MPM_JUMP_NONE;
  qa_q2f_game *game = context->game;
  qa_q2f_entity *entity = context->entity;
  qa_q2m_state *state = context->state;
  bool rerelease = q2mpm_rr(game);
  qa_body_state body = q2mpm_body(game, entity);
  if (rerelease && (!can_jump || state->jump_time > q2mpm_now(game))) return QA_Q2MPM_JUMP_NONE;
  /* TS startJump closure. */
  bool started = false;
  if (rerelease && navigation.is_path) {
    if (!navigation.traversal_pending) return QA_Q2MPM_JUMP_NONE;
    state->ideal_yaw = qa_vector_to_angles_with_rule(
        qa_normalize3_double_hypot(qa_sub3_double(navigation.first, navigation.second)), QA_VECTOR_TO_ANGLES_NATIVE_AI).y + 180;
    if (state->ideal_yaw > 360) state->ideal_yaw -= 360;
    double delta = fmod(fmod(body.angles.y - state->ideal_yaw, 360) + 360, 360);
    if (delta > 45 && delta < 315) { qa_q2m_change_yaw(context); return QA_Q2MPM_JUMP_TURN; }
    started = true;
    qa_q2m_finish_dodge(context);
    state->jump_time = q2mpm_now(game) + 3;
    return navigation.second.z > navigation.first.z ? QA_Q2MPM_JUMP_UP : QA_Q2MPM_JUMP_DOWN;
  }
  double min_z = body.origin.z + body.bounds.min.z;
  double enemy_min = enemy.origin.z + enemy.bounds.min.z;
  qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_vec3 ahead = qa_add3_double(body.origin, qa_scale3_double(forward, 48));
  bool down = enemy_min < min_z - (rerelease ? 18 : 16);
  bool up = enemy_min > min_z + (rerelease ? 18 : 16);
  if (down && drop_height != 0) {
    if (q2mpm_trace(game, body.origin, ahead, &body.bounds, &entity->actor.id, qa_q2m_monster_solid_mask(game)).fraction < 1)
      return QA_Q2MPM_JUMP_NONE;
    qa_vec3 end = ahead;
    end.z = (rerelease ? min_z : body.bounds.min.z) - drop_height - 1;
    qa_trace_result trace = q2mpm_trace(game, ahead, end, NULL, &entity->actor.id, (double)((int32_t)qa_q2m_monster_solid_mask(game) | 56));
    if (trace.fraction == 1 || trace.all_solid || trace.start_solid) return QA_Q2MPM_JUMP_NONE;
    if (rerelease && ((int32_t)trace_contents(trace) & 32) != 0) {
      qa_trace_result deep = q2mpm_trace(game, trace.end, end, NULL, &entity->actor.id, qa_q2m_monster_solid_mask(game));
      qa_vec3 probe = deep.end;
      probe.z += body.bounds.min.z + 49;
      if (((int32_t)game->host->point_contents(game->host->ctx, probe) & 56) != 0) return QA_Q2MPM_JUMP_NONE;
    }
    if (min_z - trace.end.z < 24 || ((int32_t)trace_contents(trace) & (rerelease ? 35 : 3)) == 0
        || enemy_min - trace.end.z > 32 || !trace.contact.has_plane
        || trace.contact.plane.normal.z < 0.9) return QA_Q2MPM_JUMP_NONE;
    if (rerelease) { qa_q2m_finish_dodge(context); state->jump_time = q2mpm_now(game) + 3; }
    (void)started;
    return QA_Q2MPM_JUMP_DOWN;
  }
  if (up && jump_height != 0) {
    qa_vec3 from = ahead;
    from.z = body.origin.z + body.bounds.max.z + jump_height;
    qa_trace_result trace = q2mpm_trace(game, from, ahead, NULL, &entity->actor.id, (double)((int32_t)qa_q2m_monster_solid_mask(game) | 56));
    if (trace.fraction == 1 || trace.all_solid || trace.start_solid || trace.end.z - min_z > jump_height
        || ((int32_t)trace_contents(trace) & (rerelease ? 35 : 3)) == 0) return QA_Q2MPM_JUMP_NONE;
    qa_trace_result wall = q2mpm_trace(game, body.origin, qa_add3_double(body.origin, qa_scale3_double(forward, 64)),
                                       NULL, &entity->actor.id, qa_q2m_monster_solid_mask(game));
    if (wall.fraction < 1 && !wall.all_solid && !wall.start_solid && wall.contact.has_plane) {
      state->ideal_yaw = qa_vector_to_angles_with_rule(wall.contact.plane.normal, QA_VECTOR_TO_ANGLES_NATIVE_AI).y + 180;
      if (state->ideal_yaw > 360) state->ideal_yaw -= 360;
      qa_q2m_change_yaw(context);
    }
    if (rerelease) { qa_q2m_finish_dodge(context); state->jump_time = q2mpm_now(game) + 3; }
    return QA_Q2MPM_JUMP_UP;
  }
  return QA_Q2MPM_JUMP_NONE;
}
qa_q2mpm_jump_result qa_q2mpm_blocked_check_jump_default(qa_q2m_context *context, double distance,
                                                         double drop_height, double jump_height) {
  qa_q2mpm_jump_navigation navigation;
  memset(&navigation, 0, sizeof navigation);
  return qa_q2mpm_blocked_check_jump(context, distance, drop_height, jump_height, true, navigation);
}
