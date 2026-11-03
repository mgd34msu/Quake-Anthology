/* Port of src/content/q1/addons/monsters/ordinary/army.ts.
 * quakec_{mg1,mg3}/monsters/soldier.qc and fight.qc SoldierCheckAttack. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ARMY_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ARMY_H

#include "qa/content/q1/base/monsters.h"

extern const qa_q1_base_monster_species qa_q1am_army_species; /* TS armySpecies */
/* The TS armyActions closures (entry fns of qa_q1am_army_actions). */
void qa_q1am_army_fire(void *ctx, qa_q1_base_monster *monster);
void qa_q1am_army_refire(void *ctx, qa_q1_base_monster *monster);
/* TS armyActions (Map order). */
extern const qa_q1_monster_action_entry qa_q1am_army_actions[];
extern const size_t qa_q1am_army_action_count;
bool qa_q1am_army_attack(qa_q1_base_monster *monster);
void qa_q1am_army_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker, double damage, bool nightmare_resistance);

#endif
