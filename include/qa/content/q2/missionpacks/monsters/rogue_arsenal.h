/* Port of src/content/q2/missionpacks/monsters/rogue-arsenal.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_ARSENAL_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_ARSENAL_H

#include <stddef.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/state.h"

/* Writes 3 definitions (tank, tank commander, chick); returns 3. */
size_t qa_q2mpm_create_rogue_arsenal_monsters(qa_q2mpm_arena *arena, qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                              qa_q2mpm_monster_state *source);

#endif
