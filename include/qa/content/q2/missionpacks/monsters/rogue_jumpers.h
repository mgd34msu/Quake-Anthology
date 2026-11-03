/* Port of src/content/q2/missionpacks/monsters/rogue-jumpers.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_JUMPERS_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_JUMPERS_H

#include <stddef.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/state.h"

/* TS owner: src/content/q2/base/monsters/mutant.ts
 * createMutantDefinition(monsters, source), via the landed base
 * port; edition is the TS source ("classic"|"rogue"). */
qa_q2m_definition qa_q2mpm_base_create_mutant_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters, const char *edition);

/* Writes 3 definitions (berserk, mutant, parasite); returns 3. */
size_t qa_q2mpm_create_rogue_jumping_monsters(qa_q2mpm_arena *arena, qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                              qa_q2mpm_monster_state *source);

#endif
