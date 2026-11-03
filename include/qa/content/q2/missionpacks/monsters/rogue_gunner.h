/* Port of src/content/q2/missionpacks/monsters/rogue-gunner.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_GUNNER_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_GUNNER_H

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/state.h"

qa_q2m_definition qa_q2mpm_create_rogue_gunner_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters,
                                                          qa_q2mpm_monster_state *source);

#endif
