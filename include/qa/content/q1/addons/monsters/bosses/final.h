/* Port of src/content/q1/addons/monsters/bosses/final.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_FINAL_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_FINAL_H

#include "qa/content/q1/addons/context.h"

/* TS class Q1FinalBoss extends BaseMonster. */
typedef struct qa_q1am_final_boss {
  qa_q1_base_monster base;
  qa_q1ad_context *context;
} qa_q1am_final_boss;

/* TS new Q1FinalBoss(context, entity), in place. False only on OOM. */
bool qa_q1am_final_boss_init(qa_q1am_final_boss *boss, qa_q1ad_context *context, qa_q1_actor *entity);
/* instanceof Q1FinalBoss. */
bool qa_q1am_is_final_boss(const qa_q1_base_monster *monster);
void qa_q1am_final_boss_action(qa_q1am_final_boss *boss, const char *name);
void qa_q1am_final_boss_face(qa_q1am_final_boss *boss); /* TS bossFace */
void qa_q1am_final_boss_upgrade(qa_q1am_final_boss *boss);
qa_vec3 qa_q1am_final_boss_muzzle(qa_q1am_final_boss *boss, qa_vec3 offset, qa_vec3 angles);
void qa_q1am_final_boss_missile(qa_q1am_final_boss *boss, qa_vec3 offset);
void qa_q1am_final_boss_melee_check(qa_q1am_final_boss *boss);
void qa_q1am_final_boss_missile_spawner(qa_q1am_final_boss *boss, qa_vec3 offset);
void qa_q1am_final_boss_blast(qa_q1am_final_boss *boss, qa_vec3 offset, double spread, bool effect);
void qa_q1am_final_boss_line(qa_q1am_final_boss *boss, qa_vec3 offset);
void qa_q1am_boss_teleport(qa_q1ad_context *context, bool variant);
void qa_q1am_register_final_boss(qa_q1ad_context *context);
/* C ownership (TS GC): frees the registerFinalBoss closure state of the
 * game. Call after the last use of the game. */
void qa_q1am_release_final_boss(qa_q1_services *game);

#endif
