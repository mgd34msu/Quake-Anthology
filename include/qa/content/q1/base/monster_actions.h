/* Port of src/content/q1/base/monster-actions.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_MONSTER_ACTIONS_H
#define QA_CONTENT_Q1_BASE_MONSTER_ACTIONS_H

#include "qa/content/q1/base/monsters.h"

void qa_q1_monster_action(qa_q1_base_monster *monster, const char *name);
void qa_q1_monster_jump_touch(qa_q1_base_monster *monster, qa_actor_id other);
void qa_q1_wizard_fast_fire(qa_q1b_game *game, qa_q1b_actor *timer);

#endif
