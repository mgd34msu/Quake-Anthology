/* Port of src/content/q1/addons/monsters/bosses/szombie.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_SZOMBIE_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_SZOMBIE_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

typedef struct qa_q1am_shub_zombie {
  qa_q1am_monster base;
  qa_q1am_monster_ops super_ops;
  qa_q1am_context *context;
} qa_q1am_shub_zombie;

void qa_q1am_shub_zombie_init(qa_q1am_shub_zombie *zombie, qa_q1am_context *context, qa_q1am_actor *entity);
void qa_q1am_shub_zombie_action(qa_q1am_shub_zombie *zombie, const char *name);
qa_q1am_actor *qa_q1am_spawn_shub_zombie(qa_q1am_context *context);
qa_q1am_actor *qa_q1am_spawn_homing_flame(qa_q1am_context *context, qa_q1am_actor *source);
void qa_q1am_register_shub_zombie(qa_q1am_context *context);

#endif
