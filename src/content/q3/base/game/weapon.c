/* Port of src/content/q3/base/game/weapon.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/weapon.h"
#include <math.h>
#include <stdio.h>
#include "qa/content/q3/base/game/hitscan.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h"
double qa_q3_weapon_damage_factor(qa_q3_game_client *client, double quad_factor, qa_q3_game_product product) {
  double factor = qa_q3_game_slots_get_mirror(&client->ps.powerups, 1) != 0 ? (float)quad_factor : 1;
  if (product == QA_Q3_PRODUCT_MISSIONPACK && client->persistant_powerup && client->persistant_powerup->item && client->persistant_powerup->item->tag == 12)
    factor = (float)(factor * 2);
  return factor;
}
qa_q3_sphere_intersections qa_q3_ray_sphere_intersections(qa_vec3 origin, double radius, qa_vec3 point, qa_q3_missile_direction *direction) {
  qa_vec3 dir = qa_normalize3((qa_vec3){direction->x, direction->y, direction->z});
  direction->x = dir.x; direction->y = dir.y; direction->z = dir.z;
  qa_vec3 offset = qa_sub3(point, origin);
  double b = (float)(2 * qa_dot3(dir, offset));
  double c = (float)(qa_dot3(offset, offset) - (float)(radius * radius));
  double disc = (float)((float)(b * b) - (float)(4 * c));
  qa_q3_sphere_intersections r; r.count = 0;
  if (disc > 0) {
    double root = (float)sqrt(disc);
    double first = (float)((float)(-b + root) / 2), second = (float)((float)(-b - root) / 2);
    r.points[0] = qa_add3(point, qa_scale3(dir, first)); r.points[1] = qa_add3(point, qa_scale3(dir, second)); r.count = 2;
  } else if (disc == 0) { r.points[0] = qa_add3(point, qa_scale3(dir, (float)(-b / 2))); r.count = 1; }
  return r;
}
qa_q3_weapon_invuln_impact qa_q3_invulnerability_effect(qa_q3_entity_pool *pool, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point) {
  qa_q3_weapon_invuln_impact miss = {QA_Q3_WEAPON_INVULN_MISS, {0,0,0}, {0,0,0}};
  if (pool->options.product != QA_Q3_PRODUCT_MISSIONPACK) qa_range_error("Invulnerability effects require missionpack");
  if (!target->client) return miss;
  qa_q3_missile_direction backwards = {-direction.x, -direction.y, -direction.z};
  qa_q3_sphere_intersections hits = qa_q3_ray_sphere_intersections(target->client->ps.origin, 42, point, &backwards);
  if (hits.count == 0) return miss;
  qa_vec3 impact_point = hits.points[0];
  /* EV_INVUL_IMPACT = 89 in movement constants; temp entity mirrors TS. */
  qa_q3_game_entity *impact = qa_q3_entity_pool_temp_entity(pool, target->client->ps.origin, 89);
  qa_vec3 offset = qa_sub3(impact_point, target->client->ps.origin), angles = qa_vector_to_angles(offset);
  double pitch = (float)(angles.x + 90); if (pitch > 360) pitch = (float)(pitch - 360);
  impact->s.angles = (qa_vec3){pitch, angles.y, angles.z};
  qa_q3_weapon_invuln_impact hit = {QA_Q3_WEAPON_INVULN_HIT, impact_point, qa_normalize3(offset)}; return hit;
}
static qa_q3_accuracy_subject accuracy_subject(qa_q3_game_entity *e, char *team_buf) {
  qa_q3_accuracy_subject s; s.actor = qa_q3_game_entity_actor(e).id; s.damageable = qa_q3_game_entity_takedamage(e);
  s.player = e->client != NULL; s.health = e->client ? e->client->ps.health : qa_q3_game_entity_health(e);
  if (!e->client) s.team = NULL; else { snprintf(team_buf, 64, "q3-team:%lld", (long long)e->client->sess.session_team); s.team = team_buf; }
  return s;
}
bool qa_q3_log_accuracy_hit(double game_type, qa_q3_game_entity *target, qa_q3_game_entity *attacker) {
  if (!qa_q3_game_entity_takedamage(target) || target == attacker || !target->client || !attacker->client || target->client->ps.health <= 0) return false;
  char tb[64], ab[64]; return qa_q3_accuracy_hit(game_type >= 3, accuracy_subject(target, tb), accuracy_subject(attacker, ab));
}
void qa_q3_weapon_runtime_init(qa_q3_weapon_runtime *runtime, qa_q3_weapon_host host) { runtime->host = host; }
