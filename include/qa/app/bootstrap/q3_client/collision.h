/* Port of src/app/bootstrap/q3-client/collision.ts.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_Q3_CLIENT_COLLISION_H
#define QA_APP_BOOTSTRAP_Q3_CLIENT_COLLISION_H
#include "qa/content/q3/presentation/collision_host.h"
#include "qa/contracts/scene.h"
#include "qa/world/collision/q3/counters.h"
#include "qa/world/collision/q3/settings.h"
/* Owned adapter state behind the CollisionWorld vtable. */
typedef struct qa_app_q3_client_collision {
  qa_scene_queries queries;
  qa_collision_map_settings *settings; /* borrowed */
  qa_collision_counters counters;
} qa_app_q3_client_collision;
/* TS q3ClientCollision: fills *state and returns the CollisionWorld
 * view (ctx = state; counters = &state->counters). */
qa_q3_pres_collision_world qa_app_q3_client_collision_init(
    qa_app_q3_client_collision *state, qa_scene_queries queries, qa_collision_map_settings *settings);
#endif
