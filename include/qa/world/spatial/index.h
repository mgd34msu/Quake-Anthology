/*
 * Port of src/world/spatial/index.ts: the sector spatial index.
 * TS CollisionFamily (src/world/collision/contents.ts, unported) is
 * mirrored here as qa_collision_family with the exact string values
 * compared by value. query() results are index-owned scratch.
 * GPL-2.0-or-later.
 */
#ifndef QA_WORLD_SPATIAL_INDEX_H
#define QA_WORLD_SPATIAL_INDEX_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/world.h"

/* Mirror of TS CollisionFamily (owner: src/world/collision/contents.ts):
 * exact strings "q1" | "q2" | "q3"; only "q3" is distinguished here. */
typedef const char *qa_collision_family;

/* TS ActorCollision.shape: kind "box" | "capsule" | "model"(model). */
typedef struct qa_actor_collision {
  qa_collision_family family;
  const char *shape_kind;
  double shape_model;
  double contents;
  const qa_actor_id *owner; /* nullable */
  bool has_q3_owner;
  double q3_entity_number;
  double q3_owner_number;
  const char *role; /* "solid" | "trigger" */
  bool monster;
  bool dead_monster;
  bool q1_corpse; /* TS optional literal true */
} qa_actor_collision;

/** TS SpatialActor. */
typedef struct qa_spatial_actor {
  qa_linked_body body;
  qa_actor_collision collision;
} qa_spatial_actor;

bool qa_bounds_intersect(qa_bounds first, qa_bounds second);

typedef struct qa_spatial_index qa_spatial_index;
/* NULL only on allocation failure. */
qa_spatial_index *qa_spatial_index_create(qa_bounds bounds);
void qa_spatial_index_destroy(qa_spatial_index *index);
void qa_spatial_index_link(qa_spatial_index *index, qa_linked_body body,
                           qa_actor_collision collision);
void qa_spatial_index_unlink(qa_spatial_index *index, qa_actor_id actor);
bool qa_spatial_index_get(qa_spatial_index *index, qa_actor_id actor, qa_spatial_actor *out);
/* visit returns "continue" | "stop-sector" | "stop" (exact strings). */
typedef const char *(*qa_spatial_visit_fn)(void *ctx, qa_spatial_actor actor);
void qa_spatial_index_visit(qa_spatial_index *index, qa_bounds bounds,
                            qa_spatial_visit_fn visit, void *ctx);
/* role: "solid" | "trigger" | "both". */
const qa_spatial_actor *qa_spatial_index_query(qa_spatial_index *index, qa_bounds bounds,
                                               const char *role, size_t *out_count);
void qa_spatial_index_clear(qa_spatial_index *index);

#endif
