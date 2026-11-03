/* Port of src/content/q1/missionpacks/world/rogue-tag.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_TAG_H
#define QA_CONTENT_Q1_MISSIONPACKS_WORLD_ROGUE_TAG_H
#include "qa/content/q1/missionpacks/types.h"

typedef struct qa_q1mp_world_hooks qa_q1mp_world_hooks;
/* TS RogueTag. hooks is borrowed; the struct must not move after init. */
typedef struct qa_q1mp_rogue_tag { qa_q1_services *game; const qa_q1mp_world_hooks *hooks; } qa_q1mp_rogue_tag;
void qa_q1mp_rogue_tag_init(qa_q1mp_rogue_tag *tag, qa_q1_services *game, const qa_q1mp_world_hooks *hooks);
double qa_q1mp_tag_score(qa_q1mp_rogue_tag *tag, qa_actor_id victim, qa_actor_id attacker);

#endif
