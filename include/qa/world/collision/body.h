/* Port of src/world/collision/body.ts. GPL-2.0-or-later. */
#ifndef QA_WORLD_COLLISION_BODY_H
#define QA_WORLD_COLLISION_BODY_H
#include "qa/contracts/scene.h"
#include "qa/world/spatial/index.h"
qa_trace_result qa_trace_actor_body(qa_trace_query query, qa_spatial_actor actor);
#endif
