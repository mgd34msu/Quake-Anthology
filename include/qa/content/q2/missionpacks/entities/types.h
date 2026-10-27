/* Port of src/content/q2/missionpacks/entities/types.ts. GPL-2.0-or-later.
 * qa_q2mp_mover_module mirrors the TS Q2MoverModule surface used here
 * (owner foundation/movers.ts + foundation/motion.ts linear.moveTo;
 * landed C runs on the incompatible qa_q2_* host). target_laser_think
 * mirrors q2TargetLaserThink (owner base/entities/targets.ts; landed
 * C is qa_q2_* host). centerprint/beam mirror game.host.emit kinds
 * absent from qa_q2f_presentation_event (owner foundation/host.ts). */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_TYPES_H
#define QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_TYPES_H
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

/* TS Q2MoverModule surface used by mission-pack entities. */
typedef struct qa_q2mp_mover_module {
  void *ctx;
  void (*linear_move_to)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game, qa_vec3 destination, qa_q2f_think done);
  void (*spawn_train)(void *ctx, qa_q2f_entity *entity, qa_q2f_game *game);
} qa_q2mp_mover_module;

/* TS Q2MissionPackEntityHooks. projectiles is the spawnBadArea pick
 * (the mines subobject of a qa_q2mp_projectiles chain). */
typedef struct qa_q2mp_entity_hooks {
  qa_q2mp_mover_module movers;
  qa_q2w_ballistics *weapons;
  qa_q2mp_mines *projectiles;
  void *ctx;
  void (*teleport_player)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles);
  void (*target_anger)(void *ctx, qa_q2f_entity *monster, qa_q2f_entity *target, qa_q2f_game *game);
  void (*emit)(void *ctx, qa_q2mp_entity_event event);
  qa_q2f_think target_laser_think; /* mirror; see header comment */
  void (*centerprint)(void *ctx, qa_actor_id actor, const char *text); /* mirror */
  void (*beam)(void *ctx, qa_actor_id actor, qa_vec3 start, qa_vec3 end, double width, double color, bool visible); /* mirror */
} qa_q2mp_entity_hooks;
#endif
