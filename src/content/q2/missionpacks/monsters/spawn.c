/* Port of src/content/q2/missionpacks/monsters/spawn.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/spawn.h"

#include "mp_internal.h"
#include "qa/content/q2/foundation/monsters/ai.h"
#include "qa/content/q2/missionpacks/monsters/rogue_common.h"

bool qa_q2mpm_find_rogue_spawn_point(qa_q2f_game *game, qa_vec3 start, qa_bounds bounds, double max_move_up,
                                     qa_vec3 *out) {
  double mask = (double)((uint32_t)qa_q2m_monster_solid_mask(game) | 0x10000u);
  qa_trace_result trace = q2mpm_trace(game, start, start, &bounds, NULL, mask);
  if (trace.start_solid || trace.all_solid || !qa_q2mpm_source_trace_world(game, trace)) {
    qa_vec3 raised = start;
    raised.z += max_move_up;
    trace = q2mpm_trace(game, raised, start, &bounds, NULL, qa_q2m_monster_solid_mask(game));
    if (trace.start_solid || trace.all_solid) return false;
    *out = trace.end;
    return true;
  }
  *out = start;
  return true;
}
bool qa_q2mpm_check_rogue_spawn_point(qa_q2f_game *game, qa_vec3 origin, qa_bounds bounds) {
  if ((bounds.min.x == 0 && bounds.min.y == 0 && bounds.min.z == 0)
      || (bounds.max.x == 0 && bounds.max.y == 0 && bounds.max.z == 0)) return false;
  qa_trace_result trace = q2mpm_trace(game, origin, origin, &bounds, NULL, qa_q2m_monster_solid_mask(game));
  return !trace.start_solid && !trace.all_solid && qa_q2mpm_source_trace_world(game, trace);
}
bool qa_q2mpm_check_rogue_ground_spawn_point(qa_q2f_game *game, qa_vec3 origin, qa_bounds bounds, double height,
                                             double gravity) {
  if (!qa_q2mpm_check_rogue_spawn_point(game, origin, bounds)) return false;
  qa_vec3 stop = origin;
  stop.z += bounds.min.z - height;
  double solid = qa_q2m_monster_solid_mask(game);
  qa_trace_result trace = q2mpm_trace(game, origin, stop, &bounds, NULL, (double)((uint32_t)solid | 56u));
  if (trace.fraction >= 1 || strcmp(trace.kind, "q1") == 0 || ((uint32_t)trace.contents & (uint32_t)solid) == 0)
    return false;
  qa_vec3 min = qa_add3_double(trace.end, bounds.min);
  qa_vec3 max = qa_add3_double(trace.end, bounds.max);
  const qa_vec3 corners[4] = {{min.x, min.y, 0}, {min.x, max.y, 0}, {max.x, min.y, 0}, {max.x, max.y, 0}};
  bool all = true;
  for (int i = 0; i < 4; i++) {
    qa_vec3 point = {corners[i].x, corners[i].y, gravity > 0 ? max.z + 1 : min.z - 1};
    if (game->host->point_contents(game->host->ctx, point) != 1) all = false;
  }
  if (all) return true;
  qa_vec3 start = {(min.x + max.x) * 0.5, (min.y + max.y) * 0.5, min.z};
  stop.x = start.x;
  stop.y = start.y;
  trace = q2mpm_trace(game, start, stop, NULL, NULL, solid);
  if (trace.fraction == 1) return false;
  double mid = trace.end.z + (gravity < 0 ? bounds.min.z : -bounds.max.z);
  start.z = gravity < 0 ? min.z : max.z;
  stop.z = start.z + (gravity < 0 ? -36 : 36);
  for (int i = 0; i < 4; i++) {
    qa_vec3 from = {corners[i].x, corners[i].y, start.z};
    qa_vec3 to = {corners[i].x, corners[i].y, stop.z};
    trace = q2mpm_trace(game, from, to, NULL, NULL, solid);
    if (trace.fraction == 1 || (gravity > 0 ? trace.end.z - mid : mid - trace.end.z) > 18) return false;
  }
  return true;
}
qa_q2f_entity *qa_q2mpm_create_rogue_monster(qa_q2m_monsters *monsters, qa_q2f_game *game, qa_vec3 origin,
                                             qa_vec3 angles, const char *classname) {
  qa_q2f_entity *entity = game->create(game->ctx, classname, NULL);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_angles = true; change.angles = angles;
  q2mpm_move_body(game, entity, change, true, false);
  entity->gravity_vector = q2mpm_v3(0, 0, -1);
  (void)qa_q2m_monsters_spawn_summoned(monsters, entity, game);
  entity->render_flags = q2mpm_or(entity->render_flags, 32768);
  return entity;
}
qa_q2f_entity *qa_q2mpm_create_rogue_ground_monster(qa_q2m_monsters *monsters, qa_q2f_game *game, qa_vec3 origin,
                                                    qa_vec3 angles, qa_bounds bounds, const char *classname,
                                                    double height) {
  return qa_q2mpm_check_rogue_ground_spawn_point(game, origin, bounds, height, -1)
      ? qa_q2mpm_create_rogue_monster(monsters, game, origin, angles, classname) : NULL;
}
static qa_vec3 random_angles(qa_q2f_game *game) {
  qa_vec3 angles = q2mpm_zero();
  for (int i = 0; i < 2; i++)
    angles = q2mpm_v3(floor(q2mpm_rnd(game) * 360), floor(q2mpm_rnd(game) * 360), floor(q2mpm_rnd(game) * 360));
  return angles;
}
static void spawn_grow_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game);
static qa_q2f_think spawn_grow_think(void) {
  qa_q2f_think think = {NULL, spawn_grow_think_fn};
  return think;
}
static void spawn_grow_think_fn(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game) {
  (void)ctx;
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_angles = true;
  change.angles = random_angles(game);
  q2mpm_move_body(game, entity, change, false, false);
  if (q2mpm_now(game) < entity->wait && entity->frame < 2) entity->frame++;
  if (q2mpm_now(game) >= entity->wait) {
    if (((uint32_t)entity->effects & 0x10000000u) != 0 || entity->frame == 0) {
      game->remove(game->ctx, entity);
      return;
    }
    entity->frame--;
  }
  game->show(game->ctx, entity);
  double base = entity->has_next_think ? entity->next_think : q2mpm_now(game);
  game->schedule(game->ctx, entity, base + 0.1 - q2mpm_now(game), spawn_grow_think());
}
static const qa_q2f_named_think spawn_thinks[] = {
  {"q2:rogue/spawngrow_think", {NULL, spawn_grow_think_fn}},
};
qa_q2f_callback_definitions qa_q2mpm_rogue_spawn_callbacks(void) {
  qa_q2f_callback_definitions definitions;
  memset(&definitions, 0, sizeof definitions);
  definitions.think = spawn_thinks;
  definitions.think_count = 1;
  return definitions;
}
qa_q2f_entity *qa_q2mpm_rogue_spawn_grow(qa_q2f_game *game, qa_vec3 origin, double size) {
  game->source_callbacks.register_defs(game->source_callbacks.ctx, qa_q2mpm_rogue_spawn_callbacks());
  qa_q2f_entity *entity = game->create(game->ctx, "spawngro", NULL);
  qa_q2f_body_changes change;
  memset(&change, 0, sizeof change);
  change.has_origin = true; change.origin = origin;
  change.has_angles = true; change.angles = random_angles(game);
  q2mpm_move_body(game, entity, change, true, false);
  entity->render_flags = 32768;
  entity->model = size <= 1 ? "models/items/spawngro2/tris.md2" : size == 2 ? "models/items/spawngro3/tris.md2"
                                                                              : "models/items/spawngro/tris.md2";
  entity->wait = q2mpm_now(game) + (size == 2 ? 2 : 0.3);
  if (size != 2) entity->effects = (double)((uint32_t)entity->effects | 0x10000000u);
  game->solid(game->ctx, entity, QA_Q2F_SOLID_NONE);
  game->motion(game->ctx, entity, QA_Q2F_MOTION_STATIONARY);
  game->schedule(game->ctx, entity, 0.1, spawn_grow_think());
  game->link(game->ctx, entity);
  game->show(game->ctx, entity);
  return entity;
}
