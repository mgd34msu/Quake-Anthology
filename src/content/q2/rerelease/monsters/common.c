/* Port of src/content/q2/rerelease/monsters/common.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/common.h"
#include "qa/foundation/js.h"

#include <float.h>
#include <stdlib.h>

#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "rrm_internal.h"

double qa_q2rm_host_gravity(qa_q2_game_services *game) { return game->host->gravity(game->host->ctx); }

void qa_q2rm_set_powered(qa_q2_game_services *game, qa_owned_actor actor, const char *kind, double cells) {
  qa_powered_protection_state powered = {kind, cells};
  qa_gameplay_authority_set_powered_protection(game->host->combat, actor, powered);
}

typedef struct cell_ctx {
  qa_q2_game_services *game;
  qa_owned_actor actor;
  qa_q2_entity *entity;
  double minimum_cells;
} cell_ctx;
static double cell_read(void *ctx) {
  cell_ctx *cell = ctx;
  return qa_shared_inventory_table_count(cell->game->host->inventory, cell->actor.id, "q2:monster-power");
}
static void cell_write(void *ctx, double count) {
  cell_ctx *cell = ctx;
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power";
  entry.count = count;
  double field = qa_q2_number_field(&cell->entity->spawn, "power_armor_power", cell->minimum_cells);
  entry.capacity = field > cell->minimum_cells ? field : cell->minimum_cells;
  qa_shared_inventory_table_configure(cell->game->host->inventory, cell->actor, entry, NULL, NULL);
}
void qa_q2rm_bind_armor(qa_q2m_context *context, double minimum_cells) {
  cell_ctx *cell = malloc(sizeof *cell);
  if (!cell) qa_raise("Error", 0, 0, "Out of memory binding rerelease monster power armor cells");
  cell->game = context->game;
  cell->actor = context->entity->actor;
  cell->entity = context->entity;
  cell->minimum_cells = minimum_cells;
  qa_power_armor_cell_binding binding = {cell, cell_read, cell_write};
  qa_gameplay_authority_bind_power_armor_cells(context->game->host->combat, context->entity->actor, binding);
}
void qa_q2rm_bind_armor_handler_200(void *ctx, qa_q2m_context *context) {
  (void)ctx;
  qa_q2rm_bind_armor(context, 200);
}
void qa_q2rm_init_power_armor(qa_q2m_context *context, double default_cells) {
  qa_q2_entity *entity = context->entity;
  qa_q2_game_services *game = context->game;
  if (!qa_shared_inventory_table_has(game->host->inventory, entity->actor.id))
    qa_shared_inventory_table_create_entries(game->host->inventory, entity->actor, NULL, 0);
  double cells = qa_q2_number_field(&entity->spawn, "power_armor_power", default_cells);
  double type = qa_q2_number_field(&entity->spawn, "power_armor_type", 2);
  qa_inventory_entry entry;
  memset(&entry, 0, sizeof entry);
  entry.item = "q2:monster-power";
  entry.count = cells;
  entry.capacity = cells > default_cells ? cells : default_cells;
  qa_shared_inventory_table_configure(game->host->inventory, entity->actor, entry, NULL, NULL);
  qa_q2rm_bind_armor(context, default_cells);
  qa_q2rm_set_powered(game, entity->actor, type == 0 ? "none" : type == 1 ? "screen" : "shield", cells);
}

qa_q2m_named_handler *qa_q2rm_merge_callbacks(qa_q2mpm_arena *arena, const qa_q2m_named_handler *base, size_t base_count,
                                              const qa_q2m_named_handler *extra, size_t extra_count,
                                              size_t *out_count) {
  qa_q2m_named_handler *merged = qa_q2mpm_arena_alloc(arena, (base_count + extra_count ? base_count + extra_count : 1) * sizeof *merged);
  if (!merged) qa_raise("Error", 0, 0, "Out of memory merging rerelease monster callbacks");
  size_t count = 0;
  for (size_t i = 0; i < base_count; i++) merged[count++] = base[i];
  for (size_t i = 0; i < extra_count; i++) {
    size_t j = 0;
    while (j < count && strcmp(merged[j].name, extra[i].name) != 0) j++;
    if (j < count) merged[j] = extra[i];
    else merged[count++] = extra[i];
  }
  *out_count = count;
  return merged;
}

qa_q2_rerelease_random *qa_q2rm_rerelease_random(qa_q2m_context *context) {
  qa_q2_rerelease_random *random = context->game->host->rerelease_random;
  if (!random) qa_raise("Error", 0, 0, "Rerelease monster source requires the session random stream");
  return random;
}

static double trace_contents(qa_trace_result trace) { return strcmp(trace.kind, "q1") == 0 ? 0 : trace.contents; }

bool qa_q2rm_calculate_pitch_to_fire(qa_q2m_context *context, qa_vec3 target, qa_vec3 start, qa_vec3 aim,
                                     double speed, double seconds, bool mortar, bool destroy_on_touch,
                                     qa_vec3 *out) {
  qa_q2_game_services *game = context->game;
  qa_vec3 angles = qa_vector_to_angles_with_rule(aim, QA_VECTOR_TO_ANGLES_NATIVE_AI);
  double gravity = qa_q2rm_host_gravity(game);
  double best_pitch = 0, best_distance = DBL_MAX;
  bool have_best = false;
  static const double pitches[9] = {-80, -70, -60, -50, -40, -30, -20, -10, -5};
  for (int p = 0; p < 9; p++) {
    double pitch = pitches[p];
    if (mortar && pitch >= -30) break;
    qa_vec3 pitched = angles;
    pitched.x = pitch;
    qa_vec3 velocity = qa_scale3_double(qa_math_angle_vectors_rule(pitched, QA_ANGLE_RULE_NATIVE).forward, speed);
    qa_vec3 origin = start;
    for (double remaining = seconds; remaining > 0; remaining -= 0.1) {
      velocity.z -= gravity * 0.1;
      qa_vec3 end = qa_add3_double(origin, qa_scale3_double(velocity, 0.1));
      qa_trace_result trace = q2rm_trace(game, origin, end, NULL, NULL,
                                         (double)(3 | 0x2000000 | 0x4000000 | 0x40000000));
      origin = trace.end;
      if (trace.fraction >= 1) continue;
      if (strcmp(trace.kind, "q2") == 0 && (qa_js_to_int32((trace.surface ? trace.surface->flags : 0)) & 4) != 0) break;
      qa_vec3 normal = trace.contact.has_plane ? trace.contact.plane.normal : q2rm_zero();
      origin = qa_add3_double(origin, normal);
      velocity = qa_sub3_double(velocity, qa_scale3_double(normal, qa_dot3_double(velocity, normal) * 1.6));
      qa_vec3 delta = qa_sub3_double(origin, target);
      double distance = qa_dot3_double(delta, delta);
      bool hit_enemy = trace.hit.kind == QA_TRACE_HIT_ACTOR
          && ((context->entity->has_enemy && qa_same_actor(context->entity->enemy, trace.hit.actor))
              || game->host->is_player(game->host->ctx, trace.hit.actor));
      if (hit_enemy || (normal.z >= 0.7 && distance < 128 * 128 && distance < best_distance)) {
        best_pitch = pitch; best_distance = distance; have_best = true;
      }
      if (destroy_on_touch || (qa_js_to_int32(trace_contents(trace)) & (0x2000000 | 0x4000000 | 0x40000000)) != 0) break;
    }
  }
  if (!have_best) return false;
  qa_vec3 pitched = angles;
  pitched.x = best_pitch;
  *out = qa_math_angle_vectors_rule(pitched, QA_ANGLE_RULE_NATIVE).forward;
  return true;
}
bool qa_q2rm_calculate_pitch_to_fire_default(qa_q2m_context *context, qa_vec3 target, qa_vec3 start, qa_vec3 aim,
                                             double speed, double seconds, bool mortar, qa_vec3 *out) {
  return qa_q2rm_calculate_pitch_to_fire(context, target, start, aim, speed, seconds, mortar, false, out);
}

bool qa_q2rm_chainfist(qa_q2m_context *context) {
  const qa_attack_provenance *attack = context->entity->last_attack;
  return attack && attack->cause.kind == QA_ATTACK_CAUSE_Q2 && attack->cause.means_of_death == 40;
}
bool qa_q2rm_reacts_to_pain(qa_q2m_context *context) {
  return !context->state->ducked && !context->state->combat_point
      && (context->game->options.skill < 3 || qa_q2rm_chainfist(context));
}
bool qa_q2rm_check_gib(qa_q2m_context *context) {
  const qa_attack_provenance *attack = context->entity->last_attack;
  return qa_q2m_health(context->game, &context->entity->actor.id) <= context->state->gib_health
      || (context->state->dead && attack && attack->cause.kind == QA_ATTACK_CAUSE_Q2
          && attack->cause.means_of_death == 20);
}
void qa_q2rm_monster_flash(qa_q2m_context *context, double flash, qa_vec3 origin, qa_vec3 direction) {
  qa_q2_presentation_event e;
  memset(&e, 0, sizeof e);
  e.kind = "monster-muzzleflash";
  e.has_actor = true; e.actor = context->entity->actor.id;
  e.flash = flash; e.origin = origin; e.direction = direction;
  context->game->host->emit(context->game->host->ctx, e);
}

bool qa_q2rm_predict_aim(qa_q2m_context *context, qa_vec3 start, double speed, bool eye, double offset,
                         qa_q2rm_predicted_aim *out) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(context, &enemy)) return false;
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  bool rerelease = q2rm_rr(game);
  qa_q2_entity *enemy_entity = game->entity(game->ctx, entity->has_enemy ? &entity->enemy : NULL);
  double view_height = enemy_entity ? enemy_entity->view_height : 22;
  qa_vec3 raised = enemy.origin;
  raised.z += eye ? view_height : 0;
  qa_vec3 direction = qa_sub3_double(raised, start);
  if (rerelease) {
    qa_vec3 end = qa_add3_double(start, direction);
    qa_trace_result trace = q2rm_trace(game, start, end, NULL, &entity->actor.id, qa_q2m_attack_trace_mask(game));
    if (trace.hit.kind != QA_TRACE_HIT_ACTOR || !entity->has_enemy || !qa_same_actor(trace.hit.actor, entity->enemy)) {
      eye = !eye;
      raised = enemy.origin;
      raised.z += eye ? view_height : 0;
      direction = qa_sub3_double(raised, start);
    }
  }
  double time = rerelease && speed == 0 ? 0 : qa_hypot3(direction) / speed;
  qa_vec3 target = qa_add3_double(enemy.origin, qa_scale3_double(enemy.velocity, time - offset));
  if (rerelease) {
    qa_vec3 to_target = qa_sub3_double(target, start);
    if (qa_dot3_double(qa_normalize3_double_hypot(direction), qa_normalize3_double_hypot(to_target)) < 0
        || q2rm_trace(game, start, target, NULL, NULL, 3).fraction < 0.9) target = enemy.origin;
  }
  qa_vec3 point = target;
  point.z += eye ? view_height : 0;
  out->direction = qa_normalize3_double_hypot(qa_sub3_double(point, start));
  out->point = point;
  return true;
}
bool qa_q2rm_predict_aim_default(qa_q2m_context *context, qa_vec3 start, double speed, bool eye,
                                 qa_q2rm_predicted_aim *out) {
  return qa_q2rm_predict_aim(context, start, speed, eye, 0, out);
}
bool qa_q2rm_predicted_direction(qa_q2m_context *context, qa_vec3 start, double speed, bool eye, double offset,
                                 qa_vec3 *out) {
  qa_q2rm_predicted_aim aim;
  if (!qa_q2rm_predict_aim(context, start, speed, eye, offset, &aim)) return false;
  *out = aim.direction;
  return true;
}
bool qa_q2rm_predicted_direction_default(qa_q2m_context *context, qa_vec3 start, double speed, bool eye,
                                         qa_vec3 *out) {
  return qa_q2rm_predicted_direction(context, start, speed, eye, 0, out);
}

bool qa_q2rm_blocked_check_platform(qa_q2m_context *context, double distance) {
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(context, &enemy)) return false;
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  qa_body_state body = q2rm_body(game, entity);
  bool above = enemy.origin.z + enemy.bounds.min.z >= body.origin.z + body.bounds.max.z;
  bool below = enemy.origin.z + enemy.bounds.max.z <= body.origin.z + body.bounds.min.z;
  if (!above && !below) return false;
  qa_q2_entity *platform = game->entity(game->ctx, body.ground);
  if (!platform || strncmp(platform->classname, "func_plat", 9) != 0) {
    qa_vec3 start = qa_add3_double(body.origin,
        qa_scale3_double(qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward, distance));
    qa_vec3 end = start;
    end.z += -384;
    qa_trace_result trace = q2rm_trace(game, start, end, NULL, &entity->actor.id, qa_q2m_monster_solid_mask(game));
    platform = trace.fraction < 1 && !trace.all_solid && !trace.start_solid && trace.hit.kind == QA_TRACE_HIT_ACTOR
        ? game->entity(game->ctx, &trace.hit.actor) : NULL;
  }
  if (!platform || strncmp(platform->classname, "func_plat", 9) != 0 || !platform->use.fn) return false;
  const char *state = context->platform_state(context->impl_ctx, context, platform->actor.id);
  bool standing = body.ground && qa_same_actor(*body.ground, platform->actor.id);
  bool at_bottom = state && strcmp(state, "bottom") == 0;
  bool at_top = state && strcmp(state, "top") == 0;
  if (above ? (standing && at_bottom) || (!standing && at_top)
            : (standing && at_top) || (!standing && at_bottom)) {
    platform->use.fn(platform->use.ctx, platform, game, &entity->actor.id, &entity->actor.id);
    return true;
  }
  return false;
}
bool qa_q2rm_blocked_check_platform_handler(void *ctx, qa_q2m_context *context, double distance) {
  (void)ctx;
  return qa_q2rm_blocked_check_platform(context, distance);
}

bool qa_q2rm_monster_jump_finished(qa_q2m_context *context) {
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  if (!q2rm_rr(game)) return q2rm_now(game) - entity->timestamp > 3;
  qa_body_state body = q2rm_body(game, entity);
  qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_vec3 projected = {body.velocity.x * forward.x, body.velocity.y * forward.y, body.velocity.z * forward.z};
  if (qa_hypot3(projected) < 150) {
    qa_vec3 velocity = qa_scale3_double(forward, 150);
    velocity.z = body.velocity.z;
    qa_q2_body_changes change;
    memset(&change, 0, sizeof change);
    change.has_velocity = true; change.velocity = velocity;
    q2rm_move_body(game, entity, change, false, false);
  }
  return context->state->jump_time < q2rm_now(game);
}

const char *qa_q2rm_jump_result_name(qa_q2rm_jump_result result) {
  switch (result) {
    case QA_Q2RM_JUMP_UP: return "up";
    case QA_Q2RM_JUMP_DOWN: return "down";
    case QA_Q2RM_JUMP_TURN: return "turn";
    default: return "none";
  }
}
static void start_jump(qa_q2m_context *context, bool rerelease) {
  if (!rerelease) return;
  qa_q2m_finish_dodge(context);
  context->state->jump_time = q2rm_now(context->game) + 3;
}
qa_q2rm_jump_result qa_q2rm_blocked_check_jump(qa_q2m_context *context, double distance, double drop_height,
                                               double jump_height, bool can_jump,
                                               qa_q2rm_jump_navigation navigation) {
  (void)distance;
  qa_body_state enemy;
  if (!qa_q2m_enemy_body(context, &enemy)) return QA_Q2RM_JUMP_NONE;
  qa_q2_game_services *game = context->game;
  qa_q2_entity *entity = context->entity;
  qa_q2m_state *state = context->state;
  bool rerelease = q2rm_rr(game);
  qa_body_state body = q2rm_body(game, entity);
  if (rerelease && (!can_jump || state->jump_time > q2rm_now(game))) return QA_Q2RM_JUMP_NONE;
  if (rerelease && navigation.is_path) {
    if (!navigation.traversal_pending) return QA_Q2RM_JUMP_NONE;
    qa_vec3 delta = qa_normalize3_double_hypot(qa_sub3_double(navigation.first, navigation.second));
    state->ideal_yaw = qa_vector_to_angles_with_rule(delta, QA_VECTOR_TO_ANGLES_NATIVE_AI).y + 180;
    if (state->ideal_yaw > 360) state->ideal_yaw -= 360;
    double wrapped = fmod(fmod(body.angles.y - state->ideal_yaw, 360) + 360, 360);
    if (wrapped > 45 && wrapped < 315) { qa_q2m_change_yaw(context); return QA_Q2RM_JUMP_TURN; }
    start_jump(context, rerelease);
    return navigation.second.z > navigation.first.z ? QA_Q2RM_JUMP_UP : QA_Q2RM_JUMP_DOWN;
  }
  double min_z = body.origin.z + body.bounds.min.z, enemy_min = enemy.origin.z + enemy.bounds.min.z;
  qa_vec3 forward = qa_math_angle_vectors_rule(body.angles, QA_ANGLE_RULE_NATIVE).forward;
  qa_vec3 ahead = qa_add3_double(body.origin, qa_scale3_double(forward, 48));
  bool down = enemy_min < min_z - (rerelease ? 18 : 16), up = enemy_min > min_z + (rerelease ? 18 : 16);
  double solid = qa_q2m_monster_solid_mask(game);
  if (down && drop_height != 0) {
    if (q2rm_trace(game, body.origin, ahead, &body.bounds, &entity->actor.id, solid).fraction < 1)
      return QA_Q2RM_JUMP_NONE;
    qa_vec3 end = ahead;
    end.z = (rerelease ? min_z : body.bounds.min.z) - drop_height - 1;
    qa_trace_result trace = q2rm_trace(game, ahead, end, NULL, &entity->actor.id, (double)(qa_js_to_int32(qa_js_to_uint32(solid) | 56u)));
    if (trace.fraction == 1 || trace.all_solid || trace.start_solid) return QA_Q2RM_JUMP_NONE;
    if (rerelease && (qa_js_to_int32(trace_contents(trace)) & 32) != 0) {
      qa_trace_result deep = q2rm_trace(game, trace.end, end, NULL, &entity->actor.id, solid);
      if (qa_q2m_water_above_waist_at(game, deep.end, body.bounds.min.z)) return QA_Q2RM_JUMP_NONE;
    }
    if (min_z - trace.end.z < 24 || (qa_js_to_int32(trace_contents(trace)) & (rerelease ? 35 : 3)) == 0
        || enemy_min - trace.end.z > 32 || !trace.contact.has_plane || trace.contact.plane.normal.z < 0.9)
      return QA_Q2RM_JUMP_NONE;
    start_jump(context, rerelease);
    return QA_Q2RM_JUMP_DOWN;
  }
  if (up && jump_height != 0) {
    qa_vec3 raised = ahead;
    raised.z = body.origin.z + body.bounds.max.z + jump_height;
    qa_trace_result trace = q2rm_trace(game, raised, ahead, NULL, &entity->actor.id, (double)(qa_js_to_int32(qa_js_to_uint32(solid) | 56u)));
    if (trace.fraction == 1 || trace.all_solid || trace.start_solid || trace.end.z - min_z > jump_height
        || (qa_js_to_int32(trace_contents(trace)) & (rerelease ? 35 : 3)) == 0) return QA_Q2RM_JUMP_NONE;
    qa_vec3 wall_end = qa_add3_double(body.origin, qa_scale3_double(forward, 64));
    qa_trace_result wall = q2rm_trace(game, body.origin, wall_end, NULL, &entity->actor.id, solid);
    if (wall.fraction < 1 && !wall.all_solid && !wall.start_solid && wall.contact.has_plane) {
      state->ideal_yaw = qa_vector_to_angles_with_rule(wall.contact.plane.normal, QA_VECTOR_TO_ANGLES_NATIVE_AI).y + 180;
      if (state->ideal_yaw > 360) state->ideal_yaw -= 360;
      qa_q2m_change_yaw(context);
    }
    start_jump(context, rerelease);
    return QA_Q2RM_JUMP_UP;
  }
  return QA_Q2RM_JUMP_NONE;
}
qa_q2rm_jump_result qa_q2rm_blocked_check_jump_default(qa_q2m_context *context, double distance,
                                                       double drop_height, double jump_height) {
  qa_q2rm_jump_navigation navigation;
  memset(&navigation, 0, sizeof navigation);
  return qa_q2rm_blocked_check_jump(context, distance, drop_height, jump_height, true, navigation);
}
