/* Port of src/content/q2/missionpacks/monsters/flyer.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_FLYER_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_FLYER_H

#include <stddef.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"

/* Writes 2 definitions (flyer, kamikaze) to out; returns 2. */
size_t qa_q2mpm_create_rogue_flyer_definitions(qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                               qa_q2mpm_monster_state *source);

#endif
