/* Port of src/content/q1/missionpacks/types.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/types.h"

#include "missionpacks_internal.h"

const qa_q1mp_mission_weapon_definition qa_q1mp_mission_weapons[QA_Q1MP_MISSION_WEAPONS_COUNT] = {
  {"hipnotic:laser", "progs/v_laserg.mdl", "weapon_laser_gun", "progs/g_laserg.mdl", "q1:ammo/cells", 30, 3},
  {"hipnotic:mjolnir", "progs/v_hammer.mdl", "weapon_mjolnir", "progs/g_hammer.mdl", NULL, 30, 9},
  {"hipnotic:proximity", "progs/v_prox.mdl", "weapon_proximity_gun", "progs/g_prox.mdl", "q1:ammo/rockets", 6, 5},
  {"rogue:lava-nailgun", "progs/v_lava.mdl", NULL, "progs/g_nail.mdl", "rogue:ammo/lava-nails", 0, 4},
  {"rogue:lava-supernailgun", "progs/v_lava2.mdl", NULL, "progs/g_nail2.mdl", "rogue:ammo/lava-nails", 0, 2},
  {"rogue:multi-grenade", "progs/v_multi.mdl", NULL, "progs/g_rock.mdl", "rogue:ammo/multi-rockets", 0, 6},
  {"rogue:multi-rocket", "progs/v_multi2.mdl", NULL, "progs/g_rock2.mdl", "rogue:ammo/multi-rockets", 0, 1},
  {"rogue:plasma", "progs/v_plasma.mdl", NULL, "progs/g_light.mdl", "rogue:ammo/plasma", 0, 0},
};

void qa_q1mp_set_mission_reference(qa_q1_actor *entity, const char *key, const qa_actor_id *actor) {
  if (!qa_q1_actor_reference_set(entity, key, actor)) qa_range_error("Out of memory");
}
bool qa_q1mp_mission_reference(qa_q1_services *game, qa_q1_actor *entity, const char *key, qa_actor_id *out) {
  qa_actor_id actor;
  if (!qa_q1_actor_reference_get(entity, key, &actor) ||
      !qa_session_actor_registry_is_live(game->host.actors, actor)) return false;
  *out = actor;
  return true;
}
void qa_q1mp_set_mission_number(qa_q1_actor *entity, const char *key, double value) {
  qa_q1mp_field_number(entity, key, qa_q1mp_fround(value));
}
qa_vec3 qa_q1mp_velocity_angles(qa_vec3 velocity) {
  const double yaw = velocity.x == 0 && velocity.y == 0 ? 0 : atan2(velocity.y, velocity.x) * 180 / QA_Q1MP_PI;
  const double pitch = velocity.x == 0 && velocity.y == 0 ? (velocity.z > 0 ? 90 : 270)
                       : atan2(velocity.z, qa_js_hypot2(velocity.x, velocity.y)) * 180 / QA_Q1MP_PI;
  return QA_Q1MP_V3(qa_q1mp_fround(pitch < 0 ? pitch + 360 : pitch), qa_q1mp_fround(yaw < 0 ? yaw + 360 : yaw), 0);
}
qa_vec3 qa_q1mp_grenade_velocity(qa_q1_services *game, qa_vec3 angles, qa_vec3 aimed) {
  const qa_q1_basis basis = qa_q1_make_vectors(game, angles);
  if (angles.x == 0) {
    qa_vec3 velocity = qa_q1_vscale(aimed, 600);
    velocity.z = 200;
    return velocity;
  }
  /* TS evaluates the right spread draw before the up spread draw. */
  const double right = (game->host.random(game->host.ctx) * 2 - 1) * 10;
  const double up = (game->host.random(game->host.ctx) * 2 - 1) * 10;
  return qa_q1_vadd(qa_q1_vadd(qa_q1_vadd(qa_q1_vscale(basis.forward, 600), qa_q1_vscale(basis.up, 200)),
                               qa_q1_vscale(basis.right, right)),
                    qa_q1_vscale(basis.up, up));
}
void qa_q1mp_move_missile(qa_q1_services *game, qa_q1_actor *entity, qa_vec3 velocity) {
  qa_q1_body_patch patch;
  memset(&patch, 0, sizeof patch);
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_angles = true; patch.angles = qa_q1mp_velocity_angles(qa_q1_normalize(velocity));
  patch.has_ground = true; patch.ground = NULL;
  qa_q1_set_body(game, entity, &patch);
}
