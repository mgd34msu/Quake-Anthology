/* Port of src/content/q2/missionpacks/monsters/widow2.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_WIDOW2_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_WIDOW2_H

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/types.h"

qa_q2m_definition qa_q2mpm_create_widow2_definition(qa_q2m_monsters *monsters, qa_q2mpm_weapons weapons,
                                                    qa_q2mpm_services services, qa_q2mpm_monster_state *source);

#endif
