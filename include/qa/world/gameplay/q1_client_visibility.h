/*
 * Port of src/world/gameplay/q1-client-visibility.ts.
 * Q1ClientVisibilityScene mirrors Pick<SharedSceneQueries,
 * "geometry"|"pointLeaf"|"leafCluster"|"clusterVisible"> (TS owner:
 * src/world/collision/index.ts, unported): only the geometry leaf
 * count is consumed. GPL-2.0-or-later.
 */
#ifndef QA_WORLD_GAMEPLAY_Q1_CLIENT_VISIBILITY_H
#define QA_WORLD_GAMEPLAY_Q1_CLIENT_VISIBILITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/numeric.h"
#include "qa/persistence/value.h"

/** TS Q1ClientEye. */
typedef struct qa_q1_client_eye {
  qa_vec3 origin;
  qa_vec3 view_offset;
} qa_q1_client_eye;

/** TS Q1VisibilityClient. */
typedef struct qa_q1_visibility_client {
  qa_vec3 origin;
  qa_vec3 view_offset;
  bool has_actor;
  qa_actor_id actor;
  bool free;
  double health;
  bool notarget;
} qa_q1_visibility_client;

/** TS Q1ClientVisibilityState. checkedCluster nullable. */
typedef struct qa_q1_client_visibility_state {
  double last_check_slot;
  double last_check_time;
  bool has_checked_cluster;
  double checked_cluster;
} qa_q1_client_visibility_state;

/** Mirror of Q1ClientVisibilityScene (see header note). */
typedef struct qa_q1_client_visibility_scene {
  void *ctx;
  size_t leaf_count; /* geometry.leaves.length */
  double (*point_leaf)(void *ctx, qa_vec3 point);
  double (*leaf_cluster)(void *ctx, double leaf);
  bool (*cluster_visible)(void *ctx, double from, double to, const char *kind);
} qa_q1_client_visibility_scene;

typedef struct qa_q1_client_visibility qa_q1_client_visibility;
/* NULL only on OOM; invalid max_clients aborts (TS RangeError). */
qa_q1_client_visibility *qa_q1_client_visibility_create(
    int64_t max_clients, void *client_ctx,
    qa_q1_visibility_client (*client)(void *ctx, int64_t slot),
    qa_q1_client_visibility_scene visibility);
void qa_q1_client_visibility_destroy(qa_q1_client_visibility *visibility);
bool qa_q1_client_visibility_check(qa_q1_client_visibility *visibility, qa_q1_client_eye observer,
                                   double time_seconds, qa_numeric_operations numeric,
                                   qa_actor_id *out);
qa_q1_client_visibility_state qa_q1_client_visibility_capture(qa_q1_client_visibility *visibility);
/* TS restore(unknown): a checkpoint value tree. */
void qa_q1_client_visibility_restore(qa_q1_client_visibility *visibility,
                                     const qa_save_value *value);

#endif
