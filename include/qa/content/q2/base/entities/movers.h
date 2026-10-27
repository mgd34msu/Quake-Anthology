/* Port of src/content/q2/base/entities/movers.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_ENTITIES_MOVERS_H
#define QA_CONTENT_Q2_BASE_ENTITIES_MOVERS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/entities/types.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/motion.h"
#include "qa/contracts/session.h"

/* TS Q2PlatformState.phase keeps its TS string spelling. */
typedef struct qa_q2_base_platform_state {
  qa_vec3 top, bottom;
  const char *phase; /* "top"|"bottom"|"up"|"down" */
} qa_q2_base_platform_state;

/* TS SecretState (unexported; named for the checkpoint). */
typedef struct qa_q2_base_secret_state {
  qa_vec3 first, second, home;
  bool shootable;
  double blocked_time, message_time;
} qa_q2_base_secret_state;

typedef struct qa_q2_base_platform_entry { qa_saved_actor_id actor; qa_q2_base_platform_state state; } qa_q2_base_platform_entry;
typedef struct qa_q2_base_secret_entry { qa_saved_actor_id actor; qa_q2_base_secret_state state; } qa_q2_base_secret_entry;

/* TS Q2BaseMoversCheckpoint. Entry arrays are malloc'd by capture;
 * the caller frees each array with free(3). */
typedef struct qa_q2_base_movers_checkpoint {
  qa_q2_base_platform_entry *platforms; size_t platform_count;
  qa_q2_base_secret_entry *secrets; size_t secret_count;
  qa_q2_linear_motion_checkpoint linear;
} qa_q2_base_movers_checkpoint;

/* TS traversal() result; has_value false is TS null. */
typedef struct qa_q2_base_traversal {
  bool has_value;
  bool locked;
  bool has_destination;
  qa_vec3 destination;
} qa_q2_base_traversal;

typedef struct qa_q2_base_mover_entities qa_q2_base_mover_entities;
/* NULL only on OOM. */
qa_q2_base_mover_entities *qa_q2_base_mover_entities_create(qa_q2_base_entity_hooks hooks);
void qa_q2_base_mover_entities_destroy(qa_q2_base_mover_entities *m);
/* TS readonly linear field. */
qa_q2_linear_motion *qa_q2_base_mover_entities_linear(qa_q2_base_mover_entities *m);
qa_q2_callback_definitions qa_q2_base_mover_entities_callbacks(qa_q2_base_mover_entities *m);
qa_q2_base_movers_checkpoint qa_q2_base_mover_entities_capture(qa_q2_base_mover_entities *m, qa_q2_game_services *game);
void qa_q2_base_mover_entities_restore(qa_q2_base_mover_entities *m, qa_q2_game_services *game, qa_q2_base_movers_checkpoint checkpoint);
/* NULL is TS null. The returned state is borrowed and mutable. */
qa_q2_base_platform_state *qa_q2_base_mover_entities_platform_state(qa_q2_base_mover_entities *m, qa_q2_entity *entity);
qa_q2_base_traversal qa_q2_base_mover_entities_traversal(qa_q2_base_mover_entities *m, qa_q2_entity *entity);
bool qa_q2_base_mover_entities_spawn(qa_q2_base_mover_entities *m, qa_q2_entity *entity, qa_q2_game_services *game);

#endif
