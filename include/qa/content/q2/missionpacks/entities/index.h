/* Port of src/content/q2/missionpacks/entities/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_INDEX_H
#define QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_INDEX_H
#include "qa/content/q2/missionpacks/entities/movers.h"
#include "qa/content/q2/missionpacks/entities/rogue.h"
#include "qa/content/q2/missionpacks/entities/xatrix.h"
#include "qa/content/q2/missionpacks/types.h"

/* TS Q2MissionPackEntities. movers is TS null for xatrix (has_movers
 * false). Merged callback tables are rebuilt by callbacks(). */
typedef struct qa_q2mp_entities {
  qa_q2mp_pack pack;
  qa_q2mp_xatrix_entities xatrix;
  qa_q2mp_rogue_entities rogue;
  qa_q2mp_rogue_movers movers;
  bool has_movers;
  qa_q2f_named_think cb_think[20];
  qa_q2f_named_use cb_use[10];
  qa_q2f_named_touch cb_touch[4];
  qa_q2f_named_die cb_die[2];
  qa_q2f_named_blocked cb_blocked[2];
  size_t cb_think_count, cb_use_count, cb_touch_count, cb_die_count, cb_blocked_count;
} qa_q2mp_entities;

void qa_q2mp_entities_init(qa_q2mp_entities *entities, qa_q2mp_pack pack, qa_q2mp_entity_hooks hooks);
qa_q2f_callback_definitions qa_q2mp_entities_callbacks(qa_q2mp_entities *entities);
bool qa_q2mp_entities_spawn(qa_q2mp_entities *entities, qa_q2f_entity *entity, qa_q2f_game *game);
qa_q2mp_rogue_entities_checkpoint qa_q2mp_entities_capture(qa_q2mp_entities *entities);
void qa_q2mp_entities_restore(qa_q2mp_entities *entities, qa_q2mp_rogue_entities_checkpoint state);
#endif
