/* Port of src/content/q1/addons/monsters/ordinary/army.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ARMY_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ARMY_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

extern const qa_q1am_species qa_q1am_army_species;
extern const qa_q1am_action_entry qa_q1am_army_actions[];
extern const size_t qa_q1am_army_action_count;
void qa_q1am_army_fire_action(qa_q1am_monster *monster);
void qa_q1am_army_refire_action(qa_q1am_monster *monster);
bool qa_q1am_army_attack(qa_q1am_monster *monster);
void qa_q1am_army_pain(qa_q1am_monster *monster, bool has_attacker, qa_actor_id attacker,
                       double damage, bool nightmare_resistance);

#endif
