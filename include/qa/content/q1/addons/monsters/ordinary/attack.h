/* Port of src/content/q1/addons/monsters/ordinary/attack.ts.
 * quakec_mg3/ai.qc CheckAnyAttack and species attack policies. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ATTACK_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ATTACK_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

/* monster must be an Mg3Monster (TS parameter type). */
bool qa_q1am_mg3_ordinary_attack(qa_q1_base_monster *monster);

#endif
