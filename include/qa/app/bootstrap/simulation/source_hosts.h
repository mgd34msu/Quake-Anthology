/* Port of src/app/bootstrap/simulation/source-hosts.ts. The returned
 * hosts borrow the env storage, which must outlive them.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_SOURCE_HOSTS_H
#define QA_APP_BOOTSTRAP_SIMULATION_SOURCE_HOSTS_H
#include <stdbool.h>
#include "qa/app/bootstrap/simulation/random.h"
#include "qa/content/q1/foundation/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/contracts/numeric.h"
#include "qa/world/collision/index.h"
typedef struct qa_app_sim_actor_host_runtime {
  qa_numeric_profile numeric;
  qa_app_sim_source_random *random;
  void *ctx;
  double (*now)(void *ctx);
  double (*frame_seconds)(void *ctx);
  void (*schedule)(void *ctx, qa_owned_actor actor, const double *due_seconds);
} qa_app_sim_actor_host_runtime;
typedef struct qa_app_sim_actor_host_world {
  qa_shared_scene_queries *scene;
  qa_numeric_profile numeric;
  void *ctx;
  bool (*world_actor)(void *ctx, qa_actor_id *out);
  double (*source_order)(void *ctx, qa_actor_id first, qa_actor_id second);
  /* Q2 nearby(): actor observations + body reads. */
  const qa_actor_id *(*observations)(void *ctx, size_t *out_count);
  bool (*body_read)(void *ctx, qa_actor_id actor, qa_body_state *out);
  /* TS leafContents?: "stored" | "merged" (NULL is undefined: merged). */
  const char *leaf_contents;
} qa_app_sim_actor_host_world;
/* TS createQ1ActorHost spreads the bindings and replaces trace/contents.
 * The returned host's ctx is the env; every other member forwards to the
 * binding with the binding's own ctx. */
typedef struct qa_app_sim_q1_actor_host_env { qa_q1_host bindings; qa_app_sim_actor_host_world world; } qa_app_sim_q1_actor_host_env;
typedef struct qa_app_sim_q2_actor_host_env {
  qa_q2_foundation_host bindings; qa_app_sim_actor_host_world world;
  qa_actor_id nearby_storage[256]; size_t nearby_count;
} qa_app_sim_q2_actor_host_env;
qa_q1_host qa_app_sim_create_q1_actor_host(qa_app_sim_q1_actor_host_env *env, qa_q1_host bindings, qa_app_sim_actor_host_world world);
qa_q2_foundation_host qa_app_sim_create_q2_actor_host(qa_app_sim_q2_actor_host_env *env, qa_q2_foundation_host bindings, qa_app_sim_actor_host_world world);
#endif
