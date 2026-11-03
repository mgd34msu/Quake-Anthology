/* Port of src/content/q2/missionpacks/entities/xatrix.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_XATRIX_H
#define QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_XATRIX_H
#include "qa/content/q2/missionpacks/entities/types.h"

/* TS Q2XatrixEntities. */
typedef struct qa_q2mp_xatrix_entities {
  qa_q2mp_entity_hooks hooks;
  qa_q2_named_think cb_think[7];
  qa_q2_named_die cb_die[1];
  qa_q2_named_use cb_use[4];
  qa_q2_think alarm, repair_sparks, repair_dead, repair_fx, ambience, mal_think;
  qa_q2_die light_killed;
  qa_q2_use light_use, missile_use, nuke_use, mal_use;
} qa_q2mp_xatrix_entities;

void qa_q2mp_xatrix_entities_init(qa_q2mp_xatrix_entities *entities, qa_q2mp_entity_hooks hooks);
qa_q2_callback_definitions qa_q2mp_xatrix_entities_callbacks(qa_q2mp_xatrix_entities *entities);
bool qa_q2mp_xatrix_entities_spawn(qa_q2mp_xatrix_entities *entities, qa_q2_entity *entity, qa_q2_game_services *game);
#endif
