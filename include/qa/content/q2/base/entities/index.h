/* Port of src/content/q2/base/entities/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_ENTITIES_INDEX_H
#define QA_CONTENT_Q2_BASE_ENTITIES_INDEX_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/entities/movers.h"
#include "qa/content/q2/base/entities/scenery.h"
#include "qa/content/q2/base/entities/targets.h"
#include "qa/content/q2/base/entities/triggers.h"
#include "qa/content/q2/base/entities/turrets.h"
#include "qa/content/q2/base/entities/types.h"
#include "qa/content/q2/foundation/host.h"

/* TS Q2BaseEntitiesCheckpoint. Nested arrays are malloc'd by capture;
 * the caller frees each nested array with free(3). */
typedef struct qa_q2_base_entities_module_checkpoint {
  double version; /* 1 */
  qa_q2_base_movers_checkpoint movers;
  qa_q2_base_scenery_checkpoint scenery;
  qa_q2_turrets_checkpoint turrets;
  qa_q2_base_wind_time *wind_times; size_t wind_time_count;
} qa_q2_base_entities_module_checkpoint;

/* TS q2BaseEntityClassnames. */
extern const char *const qa_q2_base_entity_classnames[];
extern const size_t qa_q2_base_entity_classname_count;

typedef struct qa_q2_base_entity_module qa_q2_base_entity_module;
/* TS Q2BaseEntityModule constructor / createQ2BaseEntityModule.
 * NULL only on OOM. */
qa_q2_base_entity_module *qa_q2_base_entity_module_create(qa_q2_base_entity_hooks hooks);
qa_q2_base_entity_module *qa_q2_create_base_entity_module(qa_q2_base_entity_hooks hooks);
void qa_q2_base_entity_module_destroy(qa_q2_base_entity_module *m);
qa_q2_spawn_module qa_q2_base_entity_module_as_spawn(qa_q2_base_entity_module *m);
qa_q2_base_traversal qa_q2_base_entity_module_mover_traversal(qa_q2_base_entity_module *m, qa_q2_entity *entity);
qa_q2_callback_definitions qa_q2_base_entity_module_callbacks(qa_q2_base_entity_module *m);
qa_q2_base_entities_module_checkpoint qa_q2_base_entity_module_capture(qa_q2_base_entity_module *m, qa_q2_game_services *game);
/* Frees the nested arrays a capture allocated (TS GC). The linear motion
 * entries stay owned by the motion module. */
void qa_q2_base_entities_module_checkpoint_free(qa_q2_base_entities_module_checkpoint *checkpoint);
/* Restore after shared tables, foundation entities and monster
 * contexts; no source callback runs. */
void qa_q2_base_entity_module_restore(qa_q2_base_entity_module *m, qa_q2_game_services *game, qa_q2_base_entities_module_checkpoint checkpoint);
qa_q2_base_platform_state *qa_q2_base_entity_module_platform_state(qa_q2_base_entity_module *m, qa_q2_entity *entity);
bool qa_q2_base_entity_module_spawn(qa_q2_base_entity_module *m, qa_q2_entity *entity, qa_q2_game_services *game);

#endif
