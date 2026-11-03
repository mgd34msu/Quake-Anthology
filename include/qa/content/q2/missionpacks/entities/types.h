/* Port of src/content/q2/missionpacks/entities/types.ts. GPL-2.0-or-later.
 * Centerprint and beam events go through game.host.emit
 * (qa_q2_presentation_event), as in TS. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_TYPES_H
#define QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_TYPES_H
#include "qa/content/q2/foundation/movers.h"
#include "qa/content/q2/foundation/weapons/ballistics.h"
#include "qa/content/q2/missionpacks/projectiles/mines.h"

/* TS Q2MissionPackEntityEvent union on kind. */
typedef enum qa_q2mp_entity_event_kind { QA_Q2MP_ENTITY_EVENT_STEAM, QA_Q2MP_ENTITY_EVENT_FORCE_WALL } qa_q2mp_entity_event_kind;
const char *qa_q2mp_entity_event_kind_name(qa_q2mp_entity_event_kind kind);
typedef struct qa_q2mp_entity_event {
  qa_q2mp_entity_event_kind kind;
  double id; qa_vec3 origin, direction; double count, color, speed, milliseconds; /* steam */
  qa_vec3 start, end; /* force-wall (color shared) */
} qa_q2mp_entity_event;

/* TS Q2MissionPackEntityHooks. projectiles is the spawnBadArea pick
 * (the mines subobject of a qa_q2mp_projectiles chain). */
typedef struct qa_q2mp_entity_hooks {
  qa_q2_mover_module *movers; /* TS Q2MoverModule, borrowed */
  qa_q2w_ballistics *weapons;
  qa_q2mp_mines *projectiles;
  void *ctx;
  void (*teleport_player)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles);
  void (*target_anger)(void *ctx, qa_q2_entity *monster, qa_q2_entity *target, qa_q2_game_services *game);
  void (*emit)(void *ctx, qa_q2mp_entity_event event);
} qa_q2mp_entity_hooks;
#endif
