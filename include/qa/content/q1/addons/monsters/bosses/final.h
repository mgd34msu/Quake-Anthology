/* Port of src/content/q1/addons/monsters/bosses/final.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_FINAL_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_FINAL_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

typedef struct qa_q1am_final_boss {
  qa_q1am_monster base;
  qa_q1am_monster_ops super_ops;
  qa_q1am_context *context;
} qa_q1am_final_boss;

void qa_q1am_final_boss_init(qa_q1am_final_boss *boss, qa_q1am_context *context, qa_q1am_actor *entity);
void qa_q1am_final_boss_action(qa_q1am_final_boss *boss, const char *name);
void qa_q1am_final_boss_face(qa_q1am_final_boss *boss);
void qa_q1am_final_boss_upgrade(qa_q1am_final_boss *boss);
void qa_q1am_final_boss_melee_check(qa_q1am_final_boss *boss);
void qa_q1am_boss_teleport(qa_q1am_context *context, bool variant);
void qa_q1am_register_final_boss(qa_q1am_context *context);

#endif
