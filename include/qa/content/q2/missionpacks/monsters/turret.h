/* Port of src/content/q2/missionpacks/monsters/turret.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_TURRET_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_TURRET_H

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/types.h"

/* Mirrored unported surface (TS owner: src/content/q2/foundation/
 * scenery.ts throwQ2Debris; landed qa_q2_throw_debris serves the
 * incompatible qa_q2_* host). Implemented in turret.c. */
qa_q2f_entity *qa_q2mpm_throw_debris(qa_q2f_entity *self, qa_q2f_game *game, const char *model, double speed,
                                     qa_vec3 origin);

qa_q2m_definition qa_q2mpm_create_rogue_turret_definition(qa_q2m_monsters *monsters, qa_q2mpm_movers movers,
                                                           qa_q2mpm_monster_state *source);

#endif
