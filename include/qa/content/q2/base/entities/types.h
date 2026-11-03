/* Port of src/content/q2/base/entities/types.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_ENTITIES_TYPES_H
#define QA_CONTENT_Q2_BASE_ENTITIES_TYPES_H

#include <stdbool.h>

#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/foundation/movers.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"

/** TS localTime() result. */
typedef struct qa_q2_base_local_time { double hour, minute, second; } qa_q2_base_local_time;

/** TS Q2BaseEntityHooks. weapons is TS Pick<Q2Ballistics, "fireBlaster" |
 * "fireRocket"> (borrowed; only those two are called). turret_driver and
 * monster_context return TS MonsterContext (monster_context NULL = TS null);
 * the monster runner owns the context. */
typedef struct qa_q2_base_entity_hooks {
  qa_q2_mover_module *movers;
  qa_q2w_ballistics *weapons;
  void *ctx;
  void (*teleport_player)(void *ctx, qa_actor_id player, qa_vec3 origin, qa_vec3 angles);
  void (*player_push)(void *ctx, qa_actor_id player, qa_vec3 velocity);
  void (*set_actor_gravity)(void *ctx, qa_actor_id actor, double gravity);
  qa_q2_base_local_time (*local_time)(void *ctx);
  qa_q2m_context *(*turret_driver)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  qa_q2m_context *(*monster_context)(void *ctx, qa_actor_id actor);
  void (*resume_monster)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
} qa_q2_base_entity_hooks;

#endif
