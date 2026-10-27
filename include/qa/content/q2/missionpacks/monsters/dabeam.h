/* Port of src/content/q2/missionpacks/monsters/dabeam.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_DABEAM_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_DABEAM_H

#include <stdbool.h>

#include "qa/content/q2/foundation/monsters/index.h"

qa_q2f_callback_definitions qa_q2mpm_monster_dabeam_callbacks(void);
qa_q2f_think qa_q2mpm_dabeam_hit_think(void);
/* target NULL mirrors TS null. */
qa_q2f_entity *qa_q2mpm_monster_dabeam(qa_q2f_entity *owner, qa_q2f_game *game, const qa_actor_id *target,
                                       qa_vec3 origin, qa_vec3 angles, double damage, bool medic);

#endif
