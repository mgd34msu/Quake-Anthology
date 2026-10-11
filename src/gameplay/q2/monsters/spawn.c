#include "internal.h"
#include "spawn.h"
#include "qa/movement.h"


static bool trace_world(const qa_q2_game *game, const qa_trace_result *trace) {
  return trace->hit == QA_TRACE_HIT_NONE || trace->hit == QA_TRACE_HIT_WORLD ||
         (trace->hit == QA_TRACE_HIT_ACTOR && game->services.physics != NULL &&
          qa_actor_id_equal(trace->actor, game->services.physics->world_actor));
}

static uint32_t spawn_mask(const qa_q2_game *game) {
  return Q2M_MONSTER_MASK |
         (game->options.edition == QA_Q2_RERELEASE ? UINT32_C(0x40000000) : 0);
}

static bool trace_box(qa_q2_game *game, qa_vec3 start, qa_vec3 end,
                      const qa_bounds *bounds, uint32_t mask,
                      qa_trace_result *out, qa_error *error) {
  qa_trace_query query = {
      .start = start,
      .end = end,
      .shape = {.kind = bounds == NULL ? QA_SHAPE_POINT : QA_SHAPE_BOX},
      .policy = qa_collision_default_policy(QA_GAME_Q2),
  };
  if (bounds != NULL)
    query.shape.bounds = *bounds;
  query.policy.contents_mask = qa_collision_contents_mask(mask, QA_GAME_Q2);
  return qa_world_trace(game->services.world, &query, out, error);
}

bool q2m_check_spawn_point(qa_q2_game *game, qa_vec3 origin,
                            qa_bounds bounds, bool *valid, qa_error *error) {
  *valid = false;
  if ((bounds.mins.x == 0.0f && bounds.mins.y == 0.0f &&
       bounds.mins.z == 0.0f) ||
      (bounds.maxs.x == 0.0f && bounds.maxs.y == 0.0f && bounds.maxs.z == 0.0f))
    return true;

  qa_trace_result trace;
  if (!trace_box(game, origin, origin, &bounds, spawn_mask(game), &trace,
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
      !qa_vec_finite(start) || !qa_bounds_valid(bounds) ||
      !isfinite(max_move_up) || max_move_up < 0.0f) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid Rogue monster spawn-point query");
    return false;
  }
  *found = false;
  *position = start;

  qa_trace_result trace;
  if (!trace_box(game, start, start, &bounds,
                 spawn_mask(game) | UINT32_C(0x10000), &trace, error))
    return false;
  if (!trace.start_solid && !trace.all_solid && trace_world(game, &trace)) {
    *found = true;
    return true;
  }

  qa_vec3 raised = start;
  raised.z += max_move_up;
  if (!trace_box(game, raised, start, &bounds, spawn_mask(game), &trace, error))
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
      !qa_bounds_valid(bounds) || !isfinite(height) || height < 0.0f ||
      !isfinite(gravity)) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                 "Invalid Rogue ground-spawn query");
    return false;
  }
  *valid = false;
  bool clear;
  if (!q2m_check_spawn_point(game, origin, bounds, &clear, error))
    return false;
  if (!clear)
    return true;

  qa_vec3 stop = origin;
  stop.z = origin.z + bounds.mins.z - height;
  qa_trace_result trace;
  if (!trace_box(game, origin, stop, &bounds, spawn_mask(game) | UINT32_C(56),
                 &trace, error))
    return false;
  if (trace.fraction >= 1.0f ||
      ((uint32_t)qa_collision_contents_export(trace.contents, QA_GAME_Q2, trace.q1_opaque_token) & spawn_mask(game)) == 0)
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
        .policy = qa_collision_default_policy(QA_GAME_Q2),
    };
    qa_point_contents contents;
    if (!qa_world_point_contents(game->services.world, &point, &contents,
                                 error))
      return false;
    if (qa_collision_point_contents_export(contents.contents, QA_GAME_Q2, contents.q1_opaque_token) != 1) {
      all_solid = false;
      break;
    }
  }
  if (all_solid) {
    *valid = true;
    return true;
  }

  qa_vec3 start = qa_v3((minimum.x + maximum.x) * 0.5f,
                        (minimum.y + maximum.y) * 0.5f, minimum.z);
  stop.x = start.x;
  stop.y = start.y;
  if (!trace_box(game, start, stop, NULL, spawn_mask(game), &trace, error))
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
    if (!trace_box(game, corner_start, corner_stop, NULL, spawn_mask(game),
                   &trace, error))
      return false;
    if (trace.fraction == 1.0f ||
        (gravity > 0.0f ? trace.end.z - middle : middle - trace.end.z) > 18.0f)
      return true;
  }
  *valid = true;
  return true;
}

static bool drop_spawn(qa_q2_game *game, qa_vec3 start, qa_bounds bounds,
                       bool *found, qa_vec3 *position, qa_error *error) {
  qa_trace_result trace;
  if (!trace_box(game, start, start, &bounds, spawn_mask(game), &trace, error))
    return false;
  if (trace.start_solid)
    start.z += 1.0f;
  qa_vec3 end = start;
  end.z -= 256.0f;
  if (!trace_box(game, start, end, &bounds, spawn_mask(game), &trace, error))
    return false;
  *found = trace.fraction != 1.0f && !trace.all_solid && !trace.start_solid;
  if (*found)
    *position = trace.end;
  return true;
}

static bool stuck_trace(void *context, qa_vec3 start, qa_vec3 end, qa_bounds bounds,
                         qa_trace_result *trace, qa_error *error) {
  qa_q2_game *game = context;
  return trace_box(game, start, end, &bounds, spawn_mask(game), trace, error);
}

bool q2m_rerelease_find_spawn_point(qa_q2_game *game, qa_vec3 start, qa_bounds bounds,
                                   bool drop, bool *found, qa_vec3 *position,
                                   qa_error *error) {
  *found = false;
  *position = start;
  if (drop) {
    if (!drop_spawn(game, start, bounds, found, position, error))
      return false;
    if (*found)
      return true;
  }
  qa_vec3 origin = start;
  qa_q2r_slide query = {
      .context = game, .trace = stuck_trace, .origin = &origin, .bounds = bounds};
  qa_q2r_stuck_result result;
  if (!qa_move_q2r_fix_stuck(&query, &result, error))
    return false;
  if (result == QA_Q2R_NO_GOOD_POSITION)
    return true;
  if (drop)
    return drop_spawn(game, origin, bounds, found, position, error);
  *found = true;
  *position = origin;
  return true;
}

bool q2m_rerelease_check_ground_spawn(qa_q2_game *game, qa_vec3 origin,
                                     qa_bounds bounds, bool *valid, qa_error *error) {
  *valid = false;
  bool clear;
  if (!q2m_check_spawn_point(game, origin, bounds, &clear, error))
    return false;
  if (!clear)
    return true;
  float bottom = origin.z + bounds.mins.z;
  float x[2] = {origin.x + bounds.mins.x, origin.x + bounds.maxs.x};
  float y[2] = {origin.y + bounds.mins.y, origin.y + bounds.maxs.y};
  bool fast = true;
  for (unsigned i = 0; i < 2 && fast; ++i) {
    for (unsigned j = 0; j < 2; ++j) {
      qa_point_query query = {
          .point = {x[i], y[j], bottom - 1.0f},
          .policy = qa_collision_default_policy(QA_GAME_Q2)};
      qa_point_contents contents;
      if (!qa_world_point_contents(game->services.world, &query, &contents, error))
        return false;
      if (qa_collision_point_contents_export(contents.contents, QA_GAME_Q2, contents.q1_opaque_token) != 1) {
        fast = false;
        break;
      }
    }
  }
  if (fast) {
    *valid = true;
    return true;
  }
  qa_vec3 start = {origin.x, origin.y, bottom}, stop = start;
  stop.z -= 36.0f;
  qa_bounds footprint = bounds;
  footprint.mins.z = footprint.maxs.z = 0;
  qa_trace_result center;
  if (!trace_box(game, start, stop, &footprint, spawn_mask(game), &center, error))
    return false;
  if (center.fraction == 1.0f)
    return true;
  qa_vec3 half = {(bounds.maxs.x - bounds.mins.x) * 0.25f,
                  (bounds.maxs.y - bounds.mins.y) * 0.25f, 0};
  qa_bounds quadrant = {.mins = qa_vec_scale(half, -1), .maxs = half};
  float center_x = origin.x + (bounds.mins.x + bounds.maxs.x) * 0.5f;
  float center_y = origin.y + (bounds.mins.y + bounds.maxs.y) * 0.5f;
  x[0] = center_x - half.x; x[1] = center_x + half.x;
  y[0] = center_y - half.y; y[1] = center_y + half.y;
  for (unsigned i = 0; i < 2; ++i) {
    for (unsigned j = 0; j < 2; ++j) {
      start.x = stop.x = x[i];
      start.y = stop.y = y[j];
      qa_trace_result trace;
      if (!trace_box(game, start, stop, &quadrant, spawn_mask(game), &trace, error))
        return false;
      if (trace.fraction == 1.0f || center.end.z - trace.end.z > 18.0f)
        return true;
    }
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
