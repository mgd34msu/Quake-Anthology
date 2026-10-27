/* Port of src/content/q2/base/entities/scenery.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_ENTITIES_SCENERY_H
#define QA_CONTENT_Q2_BASE_ENTITIES_SCENERY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/entities/types.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/contracts/session.h"

/* TS q2ClockText. Writes into out (NUL-terminated, truncated to fit). */
void qa_q2_clock_text(double seconds, double style, char *out, size_t out_size);

typedef struct qa_q2_base_scenery_animation_entry { qa_saved_actor_id actor; double first, end; } qa_q2_base_scenery_animation_entry;
typedef struct qa_q2_base_scenery_clock_entry { qa_saved_actor_id actor; double value; } qa_q2_base_scenery_clock_entry;
/* TS Q2BaseSceneryCheckpoint. Arrays are malloc'd by capture; the
 * caller frees each array with free(3). */
typedef struct qa_q2_base_scenery_checkpoint {
  qa_q2_base_scenery_animation_entry *animations; size_t animation_count;
  qa_q2_base_scenery_clock_entry *clocks; size_t clock_count;
} qa_q2_base_scenery_checkpoint;

typedef struct qa_q2_base_scenery qa_q2_base_scenery;
/* NULL only on OOM. */
qa_q2_base_scenery *qa_q2_base_scenery_create(qa_q2_base_entity_hooks hooks);
void qa_q2_base_scenery_destroy(qa_q2_base_scenery *s);
qa_q2_callback_definitions qa_q2_base_scenery_callbacks(qa_q2_base_scenery *s);
qa_q2_base_scenery_checkpoint qa_q2_base_scenery_capture(qa_q2_base_scenery *s, qa_q2_game_services *game);
void qa_q2_base_scenery_restore(qa_q2_base_scenery *s, qa_q2_game_services *game, qa_q2_base_scenery_checkpoint checkpoint);
bool qa_q2_base_scenery_spawn(qa_q2_base_scenery *s, qa_q2_entity *entity, qa_q2_game_services *game);

#endif
