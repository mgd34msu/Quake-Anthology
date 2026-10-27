/* Port of src/content/q2/rerelease/monsters/spawn-placement.ts.
 * Quake II rerelease rogue/g_rogue_spawn.cpp, g_monster.cpp,
 * m_move.cpp. Copyright (c) ZeniMax Media Inc. GPL-2.0-or-later. */
#include "qa/content/q2/rerelease/monsters/spawn_placement.h"

#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/spawn.h"
#include "qa/core/numeric.h"
#include "qa/movement/q2/rerelease.h"
#include "rrm_internal.h"

static float f32(double value) { return (float)value; }
static qa_vec3 from_tuple(const qa_q2_vec3 v) { qa_vec3 r = {v[0], v[1], v[2]}; return r; }
static void to_tuple(qa_vec3 v, qa_q2_vec3 out) { out[0] = f32(v.x); out[1] = f32(v.y); out[2] = f32(v.z); }

typedef struct placement_ctx {
  qa_q2f_game *game;
} placement_ctx;
static qa_q2_trace placement_trace(void *ctx, qa_q2_vec3 from, qa_q2_vec3 mins, qa_q2_vec3 maxs, qa_q2_vec3 end) {
  placement_ctx *placement = ctx;
  qa_q2f_game *game = placement->game;
  qa_bounds bounds = {from_tuple(mins), from_tuple(maxs)};
  qa_trace_result trace = q2rm_trace(game, from_tuple(from), from_tuple(end), &bounds, NULL,
                                     qa_q2m_monster_solid_mask(game));
  if (strcmp(trace.kind, "q2") != 0) qa_q2f_error("Rerelease monster placement requires Q2 trace fields");
  qa_q2_trace result;
  memset(&result, 0, sizeof result);
  result.startsolid = trace.start_solid;
  result.allsolid = trace.all_solid;
  result.fraction = trace.fraction;
  to_tuple(trace.end, result.endpos);
  to_tuple(trace.source_plane.plane.normal, result.plane.normal);
  result.plane.dist = trace.source_plane.plane.distance;
  result.plane.type = trace.source_plane.type;
  result.plane.signbits = trace.source_plane.signbits;
  if (trace.has_secondary) {
    to_tuple(trace.secondary_plane.plane.normal, result.plane2.normal);
    result.plane2.dist = trace.secondary_plane.plane.distance;
    result.plane2.type = trace.secondary_plane.type;
    result.plane2.signbits = trace.secondary_plane.signbits;
  } else {
    result.plane2 = qa_q2_plane();
  }
  result.has_surface = trace.surface != NULL;
  if (trace.surface) result.surface = *trace.surface;
  result.has_surface2 = trace.secondary_surface != NULL;
  if (trace.secondary_surface) result.surface2 = *trace.secondary_surface;
  result.contents = trace.contents;
  result.has_ent = trace.hit.kind != QA_TRACE_HIT_NONE;
  result.ent = trace.hit;
  result.source = trace;
  return result;
}

static bool drop_to_floor(qa_q2f_game *game, qa_vec3 origin, qa_bounds bounds, qa_vec3 *out) {
  double mask = qa_q2m_monster_solid_mask(game);
  if (q2rm_trace(game, origin, origin, &bounds, NULL, mask).start_solid) origin.z = f32(origin.z + 1);
  qa_vec3 end = origin;
  end.z = f32(origin.z - 256);
  qa_trace_result trace = q2rm_trace(game, origin, end, &bounds, NULL, mask);
  if (trace.fraction == 1 || trace.all_solid || trace.start_solid) return false;
  *out = trace.end;
  return true;
}

bool qa_q2rm_find_rerelease_spawn_point(qa_q2f_game *game, qa_vec3 start, qa_bounds bounds, double max_move_up,
                                        bool drop, qa_vec3 *out) {
  (void)max_move_up;
  if (drop && drop_to_floor(game, start, bounds, out)) return true;
  /* TS module-level placement singleton. */
  static qa_q2_rerelease_movement_context move_ctx;
  static qa_q2_rerelease_movement movement;
  static bool placement_ready;
  if (!placement_ready) {
    qa_q2_rerelease_movement_context_init(&move_ctx);
    qa_q2_create_rerelease_movement(&movement, qa_create_numeric_operations(&qa_q3_binary32_profile),
                                    &move_ctx, false, 1);
    placement_ready = true;
  }
  qa_q2_vec3 origin, mins, maxs;
  to_tuple(start, origin);
  to_tuple(bounds.min, mins);
  to_tuple(bounds.max, maxs);
  placement_ctx ctx = {game};
  double result = qa_q2_rerelease_movement_fix_stuck_object_generic(&movement, origin, mins, maxs,
                                                                    placement_trace, &ctx);
  if (result == QA_Q2_STUCK_NO_GOOD_POSITION) return false;
  if (drop) return drop_to_floor(game, from_tuple(origin), bounds, out);
  *out = from_tuple(origin);
  return true;
}
bool qa_q2rm_find_rerelease_spawn_point_default(qa_q2f_game *game, qa_vec3 start, qa_bounds bounds,
                                                double max_move_up, qa_vec3 *out) {
  return qa_q2rm_find_rerelease_spawn_point(game, start, bounds, max_move_up, true, out);
}

bool qa_q2rm_check_rerelease_ground_spawn_point(qa_q2f_game *game, qa_vec3 origin, qa_bounds bounds,
                                                double height, double gravity) {
  (void)height; (void)gravity;
  if (!qa_q2mpm_check_rogue_spawn_point(game, origin, bounds)) return false;
  double bottom = f32(origin.z + bounds.min.z);
  bool fast = true;
  const double xs[2] = {f32(origin.x + bounds.min.x), f32(origin.x + bounds.max.x)};
  const double ys[2] = {f32(origin.y + bounds.min.y), f32(origin.y + bounds.max.y)};
  for (int i = 0; i < 2 && fast; i++)
    for (int j = 0; j < 2; j++) {
      qa_vec3 point = {xs[i], ys[j], f32(bottom - 1)};
      if (game->host->point_contents(game->host->ctx, point) != 1) { fast = false; break; }
    }
  if (fast) return true;
  double mask = qa_q2m_monster_solid_mask(game);
  qa_vec3 start = {origin.x, origin.y, bottom};
  double stop_z = f32(bottom - 36);
  qa_bounds flat = {{bounds.min.x, bounds.min.y, 0}, {bounds.max.x, bounds.max.y, 0}};
  qa_vec3 stop = start;
  stop.z = stop_z;
  qa_trace_result trace = q2rm_trace(game, start, stop, &flat, NULL, mask);
  if (trace.fraction == 1) return false;
  qa_vec3 center = {f32(origin.x + f32(f32(bounds.min.x + bounds.max.x) * 0.5f)),
                    f32(origin.y + f32(f32(bounds.min.y + bounds.max.y) * 0.5f)), bottom};
  qa_vec3 half = {f32(f32(f32(bounds.max.x - bounds.min.x) * 0.5f) * 0.5f),
                  f32(f32(f32(bounds.max.y - bounds.min.y) * 0.5f) * 0.5f), 0};
  qa_bounds quadrant = {{-half.x, -half.y, 0}, half};
  const double cxs[2] = {f32(center.x - half.x), f32(center.x + half.x)};
  const double cys[2] = {f32(center.y - half.y), f32(center.y + half.y)};
  for (int i = 0; i < 2; i++)
    for (int j = 0; j < 2; j++) {
      qa_vec3 from = {cxs[i], cys[j], bottom};
      qa_vec3 to = {cxs[i], cys[j], stop_z};
      qa_trace_result corner = q2rm_trace(game, from, to, &quadrant, NULL, mask);
      if (corner.fraction == 1 || f32(trace.end.z - corner.end.z) > 18) return false;
    }
  return true;
}
