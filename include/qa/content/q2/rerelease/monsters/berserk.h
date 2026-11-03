/* Port of src/content/q2/rerelease/monsters/berserk.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_BERSERK_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_BERSERK_H
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/foundation/monsters/index.h"

void qa_q2rm_slam_radius_damage(qa_q2m_context *context, qa_vec3 origin, double damage, double kick, double radius);
qa_q2m_definition qa_q2rm_create_rerelease_berserk_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters);
#endif
