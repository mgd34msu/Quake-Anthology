/* Port of src/content/q3/base/game/weapon.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q3_BASE_GAME_WEAPON_H
#define QA_CONTENT_Q3_BASE_GAME_WEAPON_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/missile.h"
#include "qa/content/q3/base/game/state.h"
double qa_q3_weapon_damage_factor(qa_q3_game_client *client, double quad_factor, qa_q3_game_product product);
typedef struct qa_q3_sphere_intersections { qa_vec3 points[2]; size_t count; } qa_q3_sphere_intersections;
qa_q3_sphere_intersections qa_q3_ray_sphere_intersections(qa_vec3 origin, double radius, qa_vec3 point, qa_q3_missile_direction *direction);
typedef enum qa_q3_invuln_impact_kind { QA_Q3_WEAPON_INVULN_MISS, QA_Q3_WEAPON_INVULN_HIT } qa_q3_invuln_impact_kind;
typedef struct qa_q3_weapon_invuln_impact { qa_q3_invuln_impact_kind kind; qa_vec3 impact_point, bounce_direction; } qa_q3_weapon_invuln_impact;
qa_q3_weapon_invuln_impact qa_q3_invulnerability_effect(qa_q3_entity_pool *pool, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point);
bool qa_q3_log_accuracy_hit(double game_type, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
/* TS WeaponRuntime/WeaponHost: full fire dispatch needs MissileRuntime (unresolved). */
typedef struct qa_q3_weapon_host { void *ctx; double quad_factor; } qa_q3_weapon_host;
typedef struct qa_q3_weapon_runtime { qa_q3_weapon_host host; } qa_q3_weapon_runtime;
void qa_q3_weapon_runtime_init(qa_q3_weapon_runtime *runtime, qa_q3_weapon_host host);
#endif
