/* Port of src/world/actors/triggers.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_ACTORS_TRIGGERS_H
#define QA_WORLD_ACTORS_TRIGGERS_H

#include <stdbool.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/world.h"
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/spatial/index.h"

/** TS services argument of touchQ1Triggers (Pick<> subsets). */
typedef struct qa_q1_trigger_services {
  qa_session_actor_registry *actors;
  qa_shared_body_table *bodies;
  qa_spatial_index *spatial;
  void *ctx;
  bool (*is_trigger)(void *ctx, qa_owned_actor actor);
  void (*touch)(void *ctx, qa_touch_contact contact);
} qa_q1_trigger_services;

void qa_touch_q1_triggers(qa_q1_trigger_services services, qa_owned_actor actor);

#endif
