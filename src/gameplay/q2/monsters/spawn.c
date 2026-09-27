#include "internal.h"

static bool valid_bounds(qa_bounds bounds) {
  return qa_vec_finite(bounds.mins) && qa_vec_finite(bounds.maxs) &&
         bounds.mins.x <= bounds.maxs.x && bounds.mins.y <= bounds.maxs.y &&
         bounds.mins.z <= bounds.maxs.z;
}

static bool trace_world(const qa_q2_game *game, const qa_trace_result *trace) {
  return trace->hit == QA_TRACE_HIT_NONE || trace->hit == QA_TRACE_HIT_WORLD ||
         (trace->hit == QA_TRACE_HIT_ACTOR && game->services.physics != NULL &&
          qa_actor_id_equal(trace->actor, game->services.physics->world_actor));
}

static bool trace_box(qa_q2_game *game, qa_vec3 start, qa_vec3 end,
                      const qa_bounds *bounds, uint32_t mask,
                      qa_trace_result *out, qa_error *error) {
  qa_trace_query query = {
      .start = start,
      .end = end,
      .shape = {.kind = bounds == NULL ? QA_SHAPE_POINT : QA_SHAPE_BOX},
      .policy = qa_collision_default_policy(QA_COLLISION_Q2),
  };
  if (bounds != NULL)
    query.shape.bounds = *bounds;
  query.policy.contents_mask = mask;
  return qa_world_trace(game->services.world, &query, out, error);
}

static bool check_spawn_point(qa_q2_game *game, qa_vec3 origin,
                              qa_bounds bounds, bool *valid, qa_error *error) {
  *valid = false;
  if ((bounds.mins.x == 0.0f && bounds.mins.y == 0.0f &&
       bounds.mins.z == 0.0f) ||
      (bounds.maxs.x == 0.0f && bounds.maxs.y == 0.0f && bounds.maxs.z == 0.0f))
    return true;

  qa_trace_result trace;
  if (!trace_box(game, origin, origin, &bounds, Q2M_MONSTER_MASK, &trace,
                 error))
    return false;
  *valid = !trace.start_solid && !trace.all_solid && trace_world(game, &trace);
  return true;
}

bool qa_q2_rogue_find_spawn_point(qa_q2_game *game, qa_vec3 start,
                                  qa_bounds bounds, float max_move_up,
                                  bool *found, qa_vec3 *position,
                                  qa_error *error) {
  if (game == NULL || found == NULL || position == NULL ||
      !qa_vec_finite(start) || !valid_bounds(bounds) ||
      !isfinite(max_move_up) || max_move_up < 0.0f) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid Rogue monster spawn-point query");
    return false;
  }
  *found = false;
  *position = start;

  qa_trace_result trace;
  if (!trace_box(game, start, start, &bounds,
                 Q2M_MONSTER_MASK | UINT32_C(0x10000), &trace, error))
    return false;
  if (!trace.start_solid && !trace.all_solid && trace_world(game, &trace)) {
    *found = true;
    return true;
  }

  qa_vec3 raised = start;
  raised.z += max_move_up;
  if (!trace_box(game, raised, start, &bounds, Q2M_MONSTER_MASK, &trace, error))
    return false;
  if (trace.start_solid || trace.all_solid)
    return true;
  *position = trace.end;
  *found = true;
  return true;
}

bool qa_q2_rogue_check_ground_spawn(qa_q2_game *game, qa_vec3 origin,
                                    qa_bounds bounds, float height,
                                    float gravity, bool *valid,
                                    qa_error *error) {
  if (game == NULL || valid == NULL || !qa_vec_finite(origin) ||
      !valid_bounds(bounds) || !isfinite(height) || height < 0.0f ||
      !isfinite(gravity)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid Rogue ground-spawn query");
    return false;
  }
  *valid = false;
  bool clear;
  if (!check_spawn_point(game, origin, bounds, &clear, error))
    return false;
  if (!clear)
    return true;

  qa_vec3 stop = origin;
  stop.z = origin.z + bounds.mins.z - height;
  qa_trace_result trace;
  if (!trace_box(game, origin, stop, &bounds, Q2M_MONSTER_MASK | UINT32_C(56),
                 &trace, error))
    return false;
  if (trace.fraction >= 1.0f || trace.family == QA_COLLISION_Q1 ||
      ((uint32_t)trace.contents & Q2M_MONSTER_MASK) == 0)
    return true;

  qa_vec3 minimum = qa_vec_add(trace.end, bounds.mins);
  qa_vec3 maximum = qa_vec_add(trace.end, bounds.maxs);
  qa_vec3 corners[4] = {
      qa_v3(minimum.x, minimum.y, 0.0f),
      qa_v3(minimum.x, maximum.y, 0.0f),
      qa_v3(maximum.x, minimum.y, 0.0f),
      qa_v3(maximum.x, maximum.y, 0.0f),
  };
  bool all_solid = true;
  for (size_t i = 0; i < 4; ++i) {
    qa_point_query point = {
        .point = qa_v3(corners[i].x, corners[i].y,
                       gravity > 0.0f ? maximum.z + 1.0f : minimum.z - 1.0f),
        .policy = qa_collision_default_policy(QA_COLLISION_Q2),
    };
    qa_point_contents contents;
    if (!qa_world_point_contents(game->services.world, &point, &contents,
                                 error))
      return false;
    if (contents.contents != 1)
      all_solid = false;
  }
  if (all_solid) {
    *valid = true;
    return true;
  }

  qa_vec3 start = qa_v3((minimum.x + maximum.x) * 0.5f,
                        (minimum.y + maximum.y) * 0.5f, minimum.z);
  stop.x = start.x;
  stop.y = start.y;
  if (!trace_box(game, start, stop, NULL, Q2M_MONSTER_MASK, &trace, error))
    return false;
  if (trace.fraction == 1.0f)
    return true;
  float middle =
      trace.end.z + (gravity < 0.0f ? bounds.mins.z : -bounds.maxs.z);
  start.z = gravity < 0.0f ? minimum.z : maximum.z;
  stop.z = start.z + (gravity < 0.0f ? -36.0f : 36.0f);
  for (size_t i = 0; i < 4; ++i) {
    qa_vec3 corner_start = start;
    qa_vec3 corner_stop = stop;
    corner_start.x = corner_stop.x = corners[i].x;
    corner_start.y = corner_stop.y = corners[i].y;
    if (!trace_box(game, corner_start, corner_stop, NULL, Q2M_MONSTER_MASK,
                   &trace, error))
      return false;
    if (trace.fraction == 1.0f ||
        (gravity > 0.0f ? trace.end.z - middle : middle - trace.end.z) > 18.0f)
      return true;
  }
  *valid = true;
  return true;
}

bool qa_q2_rogue_spawn_growth(qa_q2_game *game, qa_vec3 origin, unsigned size,
                              qa_error *error) {
  if (game == NULL || !qa_vec_finite(origin)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid Rogue spawn-growth request");
    return false;
  }
  return q2_spawn_growth(game, origin, size, error);
}
