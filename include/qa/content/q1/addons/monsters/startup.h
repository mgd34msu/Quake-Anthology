/* Port of src/content/q1/addons/monsters/startup.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_STARTUP_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_STARTUP_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

void qa_q1am_init_mg3_monster(qa_q1am_monster *monster, qa_q1am_context *context,
                              const char *model, double type, double size);
void qa_q1am_start_mg3_monster(qa_q1am_monster *monster, qa_q1am_context *context);
bool qa_q1am_mg3_monster_activator(qa_q1am_game *game, bool has, qa_actor_id activator, qa_actor_id *out);
void qa_q1am_register_mg3_monster_startup(qa_q1am_game *game, const char *name,
                                          qa_q1am_monster_lookup monster);
bool qa_q1am_waiting_mg3_monster(qa_q1am_game *game, qa_q1am_actor *entity);

#endif
