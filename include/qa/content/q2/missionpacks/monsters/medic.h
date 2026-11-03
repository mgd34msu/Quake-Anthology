/* Port of src/content/q2/missionpacks/monsters/medic.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_MEDIC_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_MEDIC_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/types.h"

/* False is TS null. */
bool qa_q2mpm_pick_rogue_coop_target(qa_q2m_context *context, qa_actor_id *out);
/* Writes 2 definitions (medic, medic commander); returns 2. */
size_t qa_q2mpm_create_rogue_medic_definitions(qa_q2mpm_arena *arena, qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                               qa_q2mpm_weapons weapons, qa_q2mpm_monster_state *source);

#endif
