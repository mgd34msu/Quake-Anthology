/*
 * Port of src/content/q2/base/entities/types.ts. GPL-2.0-or-later.
 * C accommodations: TS Q2Ballistics Pick<> and MonsterContext (TS
 * owners: src/content/q2/foundation/weapons/ballistics.ts and
 * src/content/q2/foundation/monsters/types.ts) landed over the
 * incompatible qa_q2f_* game shape, so they are mirrored here as
 * fn+ctx surfaces over qa_q2_game_services (qa_q2_base_monster_context
 * carries only the state fields and operations this slice uses;
 * infantryStand becomes set_move("infantry_move_stand"), matching the
 * TS body). Reconcile when the two foundation shapes merge.
 */
#ifndef QA_CONTENT_Q2_BASE_ENTITIES_TYPES_H
#define QA_CONTENT_Q2_BASE_ENTITIES_TYPES_H

#include <stdbool.h>

#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/movers.h"

/** TS localTime() result. */
typedef struct qa_q2_base_local_time { double hour, minute, second; } qa_q2_base_local_time;

/* Mirror of the MonsterContext state fields used by turrets.ts. */
typedef struct qa_q2_base_monster_state {
  double trail_time, attack_finished, gib_health;
  bool lost_sight, gibbed, stand_ground, ducked;
} qa_q2_base_monster_state;

typedef struct qa_q2_base_monster_context qa_q2_base_monster_context;
struct qa_q2_base_monster_context {
  void *impl;
  qa_q2_base_monster_state *state;
  bool (*find_target)(qa_q2_base_monster_context *context);
  /* TS visible(context) with its default enemy argument. */
  bool (*visible_enemy)(qa_q2_base_monster_context *context);
  void (*set_move)(qa_q2_base_monster_context *context, const char *name);
};

/** TS Q2BaseEntityHooks. weapons fire returns are TS Q2Entity | null. */
typedef struct qa_q2_base_entity_hooks {
  qa_q2_mover_module *movers;
  void *weapons_ctx;
  /* TS fireBlaster(self, game, start, dir, damage, speed, effects,
   * hyper, mod?): has_mod false = TS omitted means-of-death. */
  qa_q2_entity *(*fire_blaster)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game,
                                qa_vec3 start, qa_vec3 direction, double damage, double speed,
                                double effects, bool hyper, bool has_mod, double mod);
  qa_q2_entity *(*fire_rocket)(void *ctx, qa_q2_entity *self, qa_q2_game_services *game,
                               qa_vec3 start, qa_vec3 direction, double damage, double speed,
                               double radius, double radius_damage);
  void *ctx;
  void (*teleport_player)(void *ctx, qa_actor_id player, qa_vec3 origin, qa_vec3 angles);
  void (*player_push)(void *ctx, qa_actor_id player, qa_vec3 velocity);
  void (*set_actor_gravity)(void *ctx, qa_actor_id actor, double gravity);
  qa_q2_base_local_time (*local_time)(void *ctx);
  qa_q2_base_monster_context *(*turret_driver)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  qa_q2_base_monster_context *(*monster_context)(void *ctx, qa_actor_id actor); /* NULL = TS null */
  void (*resume_monster)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
} qa_q2_base_entity_hooks;

#endif
