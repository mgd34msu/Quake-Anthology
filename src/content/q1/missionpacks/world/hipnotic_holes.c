/* Port of src/content/q1/missionpacks/world/hipnotic-holes.ts (hipholes.qc:
 * shotgun bullet holes, at most ten sprites that last 300 seconds).
 * GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/world/hipnotic_holes.h"

#include "../missionpacks_internal.h"

#define HOLE_LIMIT 10

static void precache_holes(void *ctx, qa_q1_services *game) {
  (void)ctx; (void)qa_q1_services_precache_model(game, "progs/s_bullet.spr");
}
static void remove_bullethole(void *ctx, qa_q1_services *game, qa_q1_actor *hole) { (void)ctx; qa_q1_remove(game, hole); }
/* game.sourceBulletHole; ctx is the game. */
static void bullet_hole(void *ctx, qa_owned_actor shooter, qa_vec3 position, qa_vec3 normal) {
  qa_q1_services *game = ctx;
  size_t count; qa_q1_actor **holes = qa_q1mp_entities(game, &count);
  size_t live = 0;
  for (size_t i = 0; i < count; i++)
    if (qa_q1mp_streq(holes[i]->classname, "bullethole") && qa_q1_live(game, holes[i])) holes[live++] = holes[i];
  qa_q1_actor *hole = qa_q1_create(game, "bullethole", NULL, NULL);
  qa_q1mp_set_string(&hole->model, "progs/s_bullet.spr"); hole->solid = "none"; hole->movement = "none";
  const qa_vec3 direction = QA_Q1MP_V3(-normal.x, -normal.y, normal.z);
  const qa_body_state_result body = qa_shared_body_table_read(game->host.bodies, shooter.id);
  const qa_vec3 forward = qa_q1_make_vectors(game, body.has_value ? body.value.angles : QA_Q1_ZERO).forward;
  qa_q1_body_patch patch = {0};
  patch.has_origin = true; patch.origin = qa_q1_vsub(position, qa_q1_vscale(forward, 0.2));
  patch.has_bounds = true; patch.bounds = QA_Q1_POINT;
  patch.has_angles = true;
  patch.angles = qa_q1mp_velocity_angles(direction);
  qa_q1_set_body(game, hole, &patch);
  qa_q1_link(game, hole);
  qa_q1_schedule(game, hole, 300, qa_q1_callback_registry_action(game->named, hole, "hip:remove_bullethole"));
  if (live + 1 > HOLE_LIMIT && live > 0) qa_q1_remove(game, holes[0]);
  free(holes);
}
void qa_q1mp_register_hipnotic_bullet_holes(qa_q1_services *game) {
  qa_q1_add_world_precache(game, precache_holes, NULL);
  qa_q1_callback_registry_register(game->named, "hip:remove_bullethole", (qa_q1_callback_handlers){.action = remove_bullethole});
  game->source_bullet_hole.ctx = game;
  game->source_bullet_hole.fn = bullet_hole;
}
