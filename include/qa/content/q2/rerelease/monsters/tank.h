/* Port of src/content/q2/rerelease/monsters/tank.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_TANK_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_TANK_H
#include <stddef.h>
#include "qa/content/q2/base/monsters/common.h"
#include "qa/content/q2/foundation/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
/* TS createRereleaseTankDefinitions: writes 2 definitions. */
size_t qa_q2rm_create_rerelease_tank_definitions(qa_q2mpm_weapons weapons, qa_q2m_definition out[2]);
/* TS rereleaseTankStandModule. */
typedef struct qa_q2rm_tank_stand_module {
  qa_q2f_named_think think_defs[1];
  qa_q2f_named_use use_defs[1];
} qa_q2rm_tank_stand_module;
void qa_q2rm_create_tank_stand_module(qa_q2rm_tank_stand_module *out);
qa_q2bm_spawn_module qa_q2rm_tank_stand_as_spawn(qa_q2rm_tank_stand_module *module);
#endif
