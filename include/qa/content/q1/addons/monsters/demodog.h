/* Port of src/content/q1/addons/monsters/demodog.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_DEMODOG_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_DEMODOG_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

typedef struct qa_q1am_demodog {
  qa_q1am_monster base;
  qa_q1am_monster_ops super_ops;
  qa_q1am_context *context;
  struct qa_q1am_demodog *next;
} qa_q1am_demodog;

void qa_q1am_demodog_init(qa_q1am_demodog *dog, qa_q1am_context *context, qa_q1am_actor *entity);
void qa_q1am_demodog_jump_touch(qa_q1am_demodog *dog, qa_actor_id other);
/* Returns the registry head (TS ReadonlyMap values in insertion order). */
qa_q1am_demodog *qa_q1am_register_mg3_demodog(qa_q1am_context *context);

#endif
