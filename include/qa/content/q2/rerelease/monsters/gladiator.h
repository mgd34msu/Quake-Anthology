/* Port of src/content/q2/rerelease/monsters/gladiator.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_GLADIATOR_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_GLADIATOR_H
#include <stddef.h>
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
/* TS createRereleaseGladiatorDefinitions: writes 2 definitions. */
size_t qa_q2rm_create_rerelease_gladiator_definitions(qa_q2mpm_weapons weapons, qa_q2m_definition out[2]);
#endif
