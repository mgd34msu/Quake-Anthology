/* Port of src/content/q1/addons/monsters/heavy/lava-man.ts.
 * quakec_mg3/monsters/mg3_lavaman.qc. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_LAVA_MAN_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_LAVA_MAN_H

#include "qa/content/q1/addons/monsters/heavy/runtime.h"

/* TS lavaManDefinition(runtime): fills *out (the callbacks capture runtime). */
void qa_q1am_lava_man_definition(qa_q1am_mg3_heavy *runtime, qa_q1am_heavy_definition *out);
/* combat.qc T_Damage: a monster_boss cannot hurt a lava man, and everything but lightning and the laser does 80%. */
void qa_q1am_register_lava_man_damage(qa_q1ad_context *context);

#endif
