/* Port of src/content/q2/rerelease/monsters/supertank.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_SUPERTANK_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_SUPERTANK_H
#include <stddef.h>
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
/* TS createRereleaseSupertankDefinitions: writes 2 definitions. */
size_t qa_q2rm_create_rerelease_supertank_definitions(qa_q2mpm_arena *arena, qa_q2mpm_weapons weapons, bool is_n64,
                                                      qa_q2m_definition out[2]);
#endif
