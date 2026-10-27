/* Port of src/content/q1/missionpacks/grapple.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_GRAPPLE_H
#define QA_CONTENT_Q1_MISSIONPACKS_GRAPPLE_H
#include "qa/content/q1/missionpacks/types.h"

/* TS RogueGrapple. */
typedef struct qa_q1mp_rogue_grapple { qa_q1mp_game *game; } qa_q1mp_rogue_grapple;
void qa_q1mp_rogue_grapple_init(qa_q1mp_rogue_grapple *grapple, qa_q1mp_game *game);

#endif
