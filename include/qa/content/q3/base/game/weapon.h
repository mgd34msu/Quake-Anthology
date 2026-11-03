/* Port of src/content/q3/base/game/weapon.ts. GPL-2.0-or-later.
 * MissileRuntime fire calls, combat spatial trace, authority reads and
 * kamikaze area queries are host fn+ctx (landed missile.h/world mirrors;
 * TS owners ./missile.ts, ../world.ts, ./combat.ts). Hitscan dispatch
 * uses landed hitscan.h; directionToByte uses landed shared header. */
#ifndef QA_CONTENT_Q3_BASE_GAME_WEAPON_H
#define QA_CONTENT_Q3_BASE_GAME_WEAPON_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/combat.h"
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/misc.h"
#include "qa/content/q3/base/game/missile.h"
#include "qa/content/q3/base/game/projectile.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/core/game_numeric.h"
double qa_q3_weapon_damage_factor(qa_q3_game_client *client, double quad_factor, qa_q3_base_product product);
typedef struct qa_q3_sphere_intersections { qa_vec3 points[2]; size_t count; } qa_q3_sphere_intersections;
qa_q3_sphere_intersections qa_q3_ray_sphere_intersections(qa_vec3 origin, double radius, qa_vec3 point, qa_q3_missile_direction *direction);
typedef enum qa_q3_invuln_impact_kind { QA_Q3_WEAPON_INVULN_MISS, QA_Q3_WEAPON_INVULN_HIT } qa_q3_invuln_impact_kind;
typedef struct qa_q3_weapon_invuln_impact { qa_q3_invuln_impact_kind kind; qa_vec3 impact_point, bounce_direction; } qa_q3_weapon_invuln_impact;
qa_q3_weapon_invuln_impact qa_q3_invulnerability_effect(qa_q3_entity_pool *pool, qa_q3_game_entity *target, qa_vec3 direction, qa_vec3 point);
bool qa_q3_log_accuracy_hit(double game_type, qa_q3_game_entity *target, qa_q3_game_entity *attacker);
typedef qa_q3_game_entity *(*qa_q3_weapon_fire_fn)(void *ctx, qa_q3_game_entity *entity, qa_vec3 origin, qa_q3_missile_direction direction);
typedef qa_q3_game_entity *(*qa_q3_weapon_fire_nail_fn)(void *ctx, qa_q3_game_entity *entity, qa_vec3 origin,
    qa_q3_missile_direction direction, qa_vec3 right, qa_vec3 up);
typedef struct qa_q3_weapon_host {
  void *ctx; double quad_factor;
  qa_q3_game_combat_context *combat; qa_q3_base_server_world world;
  qa_game_random *random;
  bool has_damage_factor; double (*damage_factor)(void *ctx, qa_q3_game_entity *entity);
  qa_q3_weapon_fire_fn fire_grenade, fire_rocket, fire_plasma, fire_bfg, fire_grapple, fire_prox;
  qa_q3_weapon_fire_nail_fn fire_nail;
  qa_q3_base_actor_trace_result (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_actor_id *pass);
  bool (*authority_read)(void *ctx, qa_actor_id actor, double *out_health, bool *out_can_take_damage, const char **out_team);
  qa_q3_damage_participant (*participant)(void *ctx, qa_actor_id actor);
  qa_q3_game_entity *(*native_by_actor)(void *ctx, qa_actor_id actor);
  bool (*is_player)(void *ctx, qa_actor_id actor);
  void *(*unlink)(void *ctx, qa_actor_id actor); void (*restore)(void *ctx, void *token);
  size_t (*area_entities)(void *ctx, qa_bounds bounds, double *out_numbers, size_t out_cap);
  bool (*link_bounds)(void *ctx, double number, qa_bounds *out);
} qa_q3_weapon_host;
typedef struct qa_q3_weapon_runtime { qa_q3_weapon_host host; } qa_q3_weapon_runtime;
void qa_q3_weapon_runtime_init(qa_q3_weapon_runtime *runtime, qa_q3_weapon_host host);
bool qa_q3_weapon_check_gauntlet_attack(qa_q3_weapon_runtime *runtime, qa_q3_game_entity *entity);
void qa_q3_weapon_fire(qa_q3_weapon_runtime *runtime, qa_q3_game_entity *entity);
qa_q3_game_entity *qa_q3_weapon_start_kamikaze(qa_q3_weapon_runtime *runtime, qa_q3_game_entity *entity);
#endif
