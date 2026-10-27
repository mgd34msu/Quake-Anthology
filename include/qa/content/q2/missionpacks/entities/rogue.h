/* Port of src/content/q2/missionpacks/entities/rogue.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_ROGUE_H
#define QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_ROGUE_H
#include "qa/content/q2/missionpacks/entities/types.h"

/* TS Q2RogueEntitiesCheckpoint. */
typedef struct qa_q2mp_rogue_entities_checkpoint { double steam_id; } qa_q2mp_rogue_entities_checkpoint;

/* TS Q2RogueEntities. */
typedef struct qa_q2mp_rogue_entities {
  qa_q2mp_entity_hooks hooks;
  double steam_id;
  qa_q2f_named_think cb_think[3];
  qa_q2f_named_touch cb_touch[2];
  qa_q2f_named_use cb_use[6];
  qa_q2f_think steam_start, black_light, orb;
  qa_q2f_touch teleport_touch, disguise_touch;
  qa_q2f_use teleport_use, disguise_use, steam_use, anger_use, kill_players, core_use;
} qa_q2mp_rogue_entities;

void qa_q2mp_rogue_entities_init(qa_q2mp_rogue_entities *entities, qa_q2mp_entity_hooks hooks);
qa_q2f_callback_definitions qa_q2mp_rogue_entities_callbacks(qa_q2mp_rogue_entities *entities);
qa_q2mp_rogue_entities_checkpoint qa_q2mp_rogue_entities_capture(qa_q2mp_rogue_entities *entities);
void qa_q2mp_rogue_entities_restore(qa_q2mp_rogue_entities *entities, qa_q2mp_rogue_entities_checkpoint state);
bool qa_q2mp_rogue_entities_spawn(qa_q2mp_rogue_entities *entities, qa_q2f_entity *entity, qa_q2f_game *game);
#endif
