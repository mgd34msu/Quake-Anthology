/* Port of src/content/q2/missionpacks/monsters/power-armor.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_POWER_ARMOR_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_POWER_ARMOR_H

#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/world/gameplay/authority.h"

/* kind is "screen" | "shield". */
void qa_q2mpm_monster_power_armor(qa_q2m_context *context, const char *kind, double cells);
void qa_q2mpm_restore_monster_power_armor(qa_q2m_context *context);
void qa_q2mpm_restore_monster_power_armor_handler(void *ctx, qa_q2m_context *context);

#endif
