/* Port of src/content/q1/foundation/spawns.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_FOUNDATION_SPAWNS_H
#define QA_CONTENT_Q1_FOUNDATION_SPAWNS_H

#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/foundation/movers.h" /* TS re-export linkDoors */

/* TS UnknownQ1SpawnError: observable via qa_q1_last_unknown_spawn before
 * the qa_range_error abort; runtime catches by pre-checking known classes
 * on foreign worlds (qa_q1_spawn_known). */
bool qa_q1_spawn_known(qa_q1_services *game, const char *classname, qa_q1_actor *entity);
/* Classname recorded by the most recent unknown-spawn abort in
 * qa_q1_spawn_map_actor (NULL before the first); static storage. */
const char *qa_q1_last_unknown_spawn(void);
void qa_q1_spawn_map_actor(qa_q1_services *game, qa_q1_actor *entity);
void qa_q1_register_spawn_callbacks(qa_q1_services *game);
qa_q1_actor *qa_q1_spawn_teleport_fog(qa_q1_services *game, qa_vec3 origin);
qa_q1_actor *qa_q1_spawn_teledeath(qa_q1_services *game, qa_vec3 origin, qa_actor_id owner);

#endif
