/* Port of src/content/q2/missionpacks/monsters/combat.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_COMBAT_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_COMBAT_H

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/types.h"

void qa_q2mpm_rogue_heal_effects(qa_q2m_context *context);
void qa_q2mpm_cleanup_rogue_heal_target(qa_q2m_monsters *monsters, qa_q2mpm_monster_state *source,
                                        qa_q2f_entity *entity, qa_q2f_game *game);
/* The returned hooks borrow a malloc'd bundle owned by the process
 * (TS closure); services is copied into it. */
qa_q2m_source_combat_hooks qa_q2mpm_create_rogue_combat_hooks(qa_q2m_monsters *monsters,
                                                              qa_q2mpm_monster_state *source,
                                                              qa_q2mpm_services services);
void qa_q2mpm_rogue_target_anger(qa_q2m_monsters *monsters, qa_q2mpm_monster_state *source, qa_q2f_entity *entity,
                                 qa_q2f_entity *target, qa_q2f_game *game);

#endif
