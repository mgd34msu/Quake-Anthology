/* Port of src/content/q2/missionpacks/monsters/types.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_TYPES_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_TYPES_H

#include <stdbool.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/foundation/movers.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* TS Q2MonsterMissionPack: "xatrix" | "rogue". */
typedef enum qa_q2mpm_pack { QA_Q2MPM_PACK_XATRIX, QA_Q2MPM_PACK_ROGUE } qa_q2mpm_pack;
const char *qa_q2mpm_pack_name(qa_q2mpm_pack pack);

typedef struct qa_q2mpm_powerups {
  double quad_until, double_until, invulnerability_until;
} qa_q2mpm_powerups;

/* TS Q2MissionPackMonsterServices. bad_area_entity returns NULL for
 * TS null; origin NULL mirrors the omitted optional argument. */
/* Allocations made while building a module's definitions (bundles, merged
 * callback tables, wrapper definitions). qa_q2mpm_monsters_module owns one
 * and frees every entry in qa_q2mpm_monsters_destroy. */
typedef struct qa_q2mpm_arena { void **items; size_t count, cap; } qa_q2mpm_arena;
/* NULL on OOM. A NULL arena is a plain malloc the caller owns (the
 * process-lifetime static definitions in xatrix_variants.c). */
void *qa_q2mpm_arena_alloc(qa_q2mpm_arena *arena, size_t size);
void qa_q2mpm_arena_destroy(qa_q2mpm_arena *arena);

typedef struct qa_q2mpm_services {
  void *ctx;
  qa_q2_mover_module *movers; /* TS Q2MoverModule, borrowed */
  double (*gravity)(void *ctx);
  bool (*bad_area)(void *ctx, qa_actor_id actor);
  qa_q2_entity *(*bad_area_entity)(void *ctx, qa_actor_id actor, const qa_vec3 *origin);
  bool (*mark_tesla_area)(void *ctx, qa_q2_entity *self, qa_q2_entity *tesla);
  qa_q2mpm_powerups (*powerups)(void *ctx, qa_actor_id actor);
} qa_q2mpm_services;

/** TS Q2MissionPackMonsterWeapons. fire_heat_rocket's optional
 * turnFraction uses has_turn_fraction. */
typedef struct qa_q2mpm_weapons {
  void *ctx;
  qa_q2_entity *(*fire_ion_ripper)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start,
                                    qa_vec3 direction, double damage, double speed, double effects);
  qa_q2_entity *(*fire_blue_blaster)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start,
                                      qa_vec3 direction, double damage, double speed, double effects);
  qa_q2_entity *(*fire_heat_rocket)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start,
                                     qa_vec3 direction, double damage, double speed, double radius,
                                     double radius_damage, bool has_turn_fraction, double turn_fraction);
  qa_q2_entity *(*fire_plasma)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start,
                                qa_vec3 direction, double damage, double speed, double radius, double radius_damage);
  qa_q2_entity *(*fire_blaster2)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start,
                                  qa_vec3 direction, double damage, double speed, double effects);
  qa_q2_entity *(*fire_tracker)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start,
                                 qa_vec3 direction, double damage, double speed, const qa_actor_id *enemy);
  qa_q2_entity *(*fire_flechette)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start,
                                   qa_vec3 direction, double damage, double speed, double kick);
  void (*fire_heat_beam)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start,
                         qa_vec3 direction, qa_vec3 offset, double damage, double kick);
} qa_q2mpm_weapons;

#endif
