/* Port of src/content/q2/missionpacks/monsters/widow/common.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_WIDOW_COMMON_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_WIDOW_COMMON_H

#include <stdbool.h>

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/types.h"

qa_vec3 qa_q2mpm_widow_project(qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 offset);
void qa_q2mpm_widow_slots(qa_q2m_context *context);
double qa_q2mpm_widow_slots_left(qa_q2m_context *context);
void qa_q2mpm_widow_summon(qa_q2m_context *context, qa_q2m_monsters *monsters, bool second, bool grow);
void qa_q2mpm_widow_clear_powerups(qa_q2m_context *context, qa_q2mpm_monster_state *source);
void qa_q2mpm_widow_powerups(qa_q2m_context *context, qa_q2mpm_services services, qa_q2mpm_monster_state *source);
qa_q2_think qa_q2mpm_widow_power_think(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters, qa_q2mpm_monster_state *source);
void qa_q2mpm_widow_restore_armor(qa_q2m_context *context);
void qa_q2mpm_widow_restore_armor_handler(void *ctx, qa_q2m_context *context);

#endif
