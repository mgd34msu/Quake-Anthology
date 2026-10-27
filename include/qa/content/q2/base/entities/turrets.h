/* Port of src/content/q2/base/entities/turrets.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_ENTITIES_TURRETS_H
#define QA_CONTENT_Q2_BASE_ENTITIES_TURRETS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/entities/types.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/contracts/session.h"

/* TS snapQ2TurretEighth. */
double qa_q2_snap_turret_eighth(double value);

/* TS Breach (unexported; named for the checkpoint). */
typedef struct qa_q2_base_turret_breach_state {
  qa_vec3 goal, muzzle;
  double pitch_max, pitch_min, yaw_min, yaw_max;
} qa_q2_base_turret_breach_state;

typedef struct qa_q2_base_turret_breach_entry { qa_saved_actor_id actor; qa_q2_base_turret_breach_state state; } qa_q2_base_turret_breach_entry;
typedef struct qa_q2_base_turret_driver_entry {
  qa_saved_actor_id actor;
  bool has_breach;
  qa_saved_actor_id breach;
  double radius, yaw_offset, height;
  const char *monster_die;
} qa_q2_base_turret_driver_entry;

/* TS Q2TurretsCheckpoint. Arrays are malloc'd by capture; the caller
 * frees each array with free(3). */
typedef struct qa_q2_turrets_checkpoint {
  qa_q2_base_turret_breach_entry *breaches; size_t breach_count;
  qa_q2_base_turret_driver_entry *drivers; size_t driver_count;
} qa_q2_turrets_checkpoint;

typedef struct qa_q2_turret_entities qa_q2_turret_entities;
/* NULL only on OOM. */
qa_q2_turret_entities *qa_q2_turret_entities_create(qa_q2_base_entity_hooks hooks);
void qa_q2_turret_entities_destroy(qa_q2_turret_entities *t);
qa_q2_callback_definitions qa_q2_turret_entities_callbacks(qa_q2_turret_entities *t);
qa_q2_turrets_checkpoint qa_q2_turret_entities_capture(qa_q2_turret_entities *t, qa_q2_game_services *game);
void qa_q2_turret_entities_restore(qa_q2_turret_entities *t, qa_q2_game_services *game, qa_q2_turrets_checkpoint checkpoint);
bool qa_q2_turret_entities_spawn(qa_q2_turret_entities *t, qa_q2_entity *entity, qa_q2_game_services *game);

#endif
