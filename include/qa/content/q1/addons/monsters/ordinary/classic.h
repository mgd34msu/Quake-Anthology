/* Port of src/content/q1/addons/monsters/ordinary/classic.ts.
 * quakec_{mg1,mg3}/monsters/{soldier,dog}.qc: the ordinary grunt and dog under InitMonster.
 * GPL-2.0-or-later. classicFrames(program) is composed into the generated
 * ordinary source tables (ordinary/source_frames.h). */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_CLASSIC_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_CLASSIC_H

#include "qa/content/q1/base/monsters.h"

extern const qa_q1_base_monster_species qa_q1am_dog_species; /* TS dogSpecies */
/* TS classicActions = [...armyActions, army_die3, dog_bite, dog_jump]. */
extern const qa_q1_monster_action_entry qa_q1am_classic_actions[];
extern const size_t qa_q1am_classic_action_count;
/* MG1 items.qc DropBackpack returns when a horde manager exists (entry ctx = the action name). */
extern const qa_q1_monster_action_entry qa_q1am_mg1_horde_no_backpack_actions[];
extern const size_t qa_q1am_mg1_horde_no_backpack_action_count;
/* MG3 removed every ammo drop from the ordinary monsters; MG1 keeps them. */
extern const qa_q1_monster_action_entry qa_q1am_mg3_no_backpack_actions[];
extern const size_t qa_q1am_mg3_no_backpack_action_count;
void qa_q1am_dog_jump_touch(qa_q1_base_monster *monster, qa_actor_id other);
void qa_q1am_dog_pain(qa_q1_base_monster *monster, const qa_actor_id *attacker);
bool qa_q1am_dog_attack(qa_q1_base_monster *monster, double range);

#endif
