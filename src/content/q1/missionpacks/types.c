/* Port of src/content/q1/missionpacks/types.ts. GPL-2.0-or-later. */
#include "qa/content/q1/missionpacks/types.h"

#include "missionpacks_internal.h"

const qa_q1mp_mission_weapon_definition qa_q1mp_mission_weapons[8] = {
  {"hipnotic:laser", "progs/v_laserg.mdl", "weapon_laser_gun", "progs/g_laserg.mdl", "q1:ammo/cells", 30, 3},
  {"hipnotic:mjolnir", "progs/v_hammer.mdl", "weapon_mjolnir", "progs/g_hammer.mdl", NULL, 30, 9},
  {"hipnotic:proximity", "progs/v_prox.mdl", "weapon_proximity_gun", "progs/g_prox.mdl", "q1:ammo/rockets", 6, 5},
  {"rogue:lava-nailgun", "progs/v_lava.mdl", NULL, "progs/g_nail.mdl", "rogue:ammo/lava-nails", 0, 4},
  {"rogue:lava-supernailgun", "progs/v_lava2.mdl", NULL, "progs/g_nail2.mdl", "rogue:ammo/lava-nails", 0, 2},
  {"rogue:multi-grenade", "progs/v_multi.mdl", NULL, "progs/g_rock.mdl", "rogue:ammo/multi-rockets", 0, 6},
  {"rogue:multi-rocket", "progs/v_multi2.mdl", NULL, "progs/g_rock2.mdl", "rogue:ammo/multi-rockets", 0, 1},
  {"rogue:plasma", "progs/v_plasma.mdl", NULL, "progs/g_light.mdl", "rogue:ammo/plasma", 0, 0},
};

void qa_q1mp_set_mission_reference(qa_q1mp_actor *entity, const char *key, qa_q1mp_opt_actor actor) {
  qa_q1b_references_set(entity, key, actor);
}
qa_q1mp_opt_actor qa_q1mp_mission_reference(qa_q1mp_game *game, qa_q1mp_actor *entity, const char *key) {
  qa_q1mp_opt_actor actor = qa_q1b_references_get(entity, key);
  return actor.has && game->base->host->actors_is_live(game->base->host->ctx, actor.id) ? actor : QA_Q1MP_NONE;
}
void qa_q1mp_set_mission_number(qa_q1mp_actor *entity, const char *key, double value) {
  char buf[32];
  qa_q1b_format_fround(buf, sizeof buf, value);
  qa_q1b_fields_set(entity, key, buf);
}
qa_vec3 qa_q1mp_velocity_angles(qa_vec3 velocity) {
  double yaw = velocity.x == 0 && velocity.y == 0 ? 0 : atan2(velocity.y, velocity.x) * 180 / 3.141592653589793;
  double pitch = velocity.x == 0 && velocity.y == 0 ? (velocity.z > 0 ? 90 : 270)
                 : atan2(velocity.z, hypot(velocity.x, velocity.y)) * 180 / 3.141592653589793;
  return QA_Q1MP_V3(qa_q1mp_fround(pitch < 0 ? pitch + 360 : pitch), qa_q1mp_fround(yaw < 0 ? yaw + 360 : yaw), 0);
}
qa_vec3 qa_q1mp_grenade_velocity(qa_q1mp_game *game, qa_vec3 angles, qa_vec3 aimed) {
  qa_q1b_basis basis = qa_q1mp_vectors(game, angles);
  if (angles.x == 0) {
    qa_vec3 v = qa_q1mp_vscale(aimed, 600);
    v.z = 200;
    return v;
  }
  return qa_q1mp_vadd(qa_q1mp_vadd(qa_q1mp_vadd(qa_q1mp_vscale(basis.forward, 600), qa_q1mp_vscale(basis.up, 200)),
                                   qa_q1mp_vscale(basis.right, (qa_q1mp_random(game) * 2 - 1) * 10)),
                      qa_q1mp_vscale(basis.up, (qa_q1mp_random(game) * 2 - 1) * 10));
}
void qa_q1mp_move_missile(qa_q1mp_game *game, qa_q1mp_actor *entity, qa_vec3 velocity) {
  qa_q1b_body_patch patch = {0};
  patch.has_velocity = true; patch.velocity = velocity;
  patch.has_angles = true; patch.angles = qa_q1mp_velocity_angles(qa_q1mp_normalize(velocity));
  patch.has_ground = true; patch.ground = QA_Q1MP_NONE;
  game->base->set_body(game->base, entity, &patch);
}
