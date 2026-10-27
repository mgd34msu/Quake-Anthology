/* Port of src/content/q2/base/entities/targets.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_ENTITIES_TARGETS_H
#define QA_CONTENT_Q2_BASE_ENTITIES_TARGETS_H

#include <stdbool.h>

#include "qa/content/q2/base/entities/types.h"
#include "qa/content/q2/foundation/callbacks.h"

/* TS q2TargetLaserThink (a free think callback with no ctx state). */
void qa_q2_target_laser_think_fn(void *ctx, qa_q2_entity *self, qa_q2_game_services *services);
extern const qa_q2_think QA_Q2_TARGET_LASER_THINK;

typedef struct qa_q2_base_targets qa_q2_base_targets;
/* NULL only on OOM. */
qa_q2_base_targets *qa_q2_base_targets_create(qa_q2_base_entity_hooks hooks);
void qa_q2_base_targets_destroy(qa_q2_base_targets *t);
qa_q2_callback_definitions qa_q2_base_targets_callbacks(qa_q2_base_targets *t);
bool qa_q2_base_targets_spawn(qa_q2_base_targets *t, qa_q2_entity *entity, qa_q2_game_services *game);

#endif
