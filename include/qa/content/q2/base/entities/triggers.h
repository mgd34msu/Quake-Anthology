/* Port of src/content/q2/base/entities/triggers.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_ENTITIES_TRIGGERS_H
#define QA_CONTENT_Q2_BASE_ENTITIES_TRIGGERS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/entities/types.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/contracts/session.h"

/* TS capture() entry. */
typedef struct qa_q2_base_wind_time { qa_saved_actor_id actor; double until; } qa_q2_base_wind_time;

typedef struct qa_q2_environmental_triggers qa_q2_environmental_triggers;
/* NULL only on OOM. */
qa_q2_environmental_triggers *qa_q2_environmental_triggers_create(qa_q2_base_entity_hooks hooks);
void qa_q2_environmental_triggers_destroy(qa_q2_environmental_triggers *t);
qa_q2_callback_definitions qa_q2_environmental_triggers_callbacks(qa_q2_environmental_triggers *t);
/* Capture array is malloc'd; the caller frees it with free(3). */
qa_q2_base_wind_time *qa_q2_environmental_triggers_capture(qa_q2_environmental_triggers *t, qa_q2_game_services *game, size_t *out_count);
void qa_q2_environmental_triggers_restore(qa_q2_environmental_triggers *t, qa_q2_game_services *game, const qa_q2_base_wind_time *checkpoint, size_t count);
bool qa_q2_environmental_triggers_spawn(qa_q2_environmental_triggers *t, qa_q2_entity *entity, qa_q2_game_services *game);

#endif
