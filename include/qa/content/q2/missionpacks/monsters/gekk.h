/* Port of src/content/q2/missionpacks/monsters/gekk.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_GEKK_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_GEKK_H

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/types.h"

qa_q2_entity *qa_q2mpm_fire_gekk_loogie(qa_q2_entity *self, qa_q2_game_services *game, qa_vec3 start, qa_vec3 direction,
                                         double damage, double speed);
qa_q2m_definition qa_q2mpm_create_gekk_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters);

#endif
