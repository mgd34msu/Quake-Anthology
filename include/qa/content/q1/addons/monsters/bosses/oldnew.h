/* Port of src/content/q1/addons/monsters/bosses/oldnew.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_OLDNEW_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_OLDNEW_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

typedef struct qa_q1am_oldnew {
  qa_q1am_monster base;
  qa_q1am_monster_ops super_ops;
  qa_q1am_context *context;
} qa_q1am_oldnew;

void qa_q1am_oldnew_init(qa_q1am_oldnew *oldnew, qa_q1am_context *context, qa_q1am_actor *entity);
void qa_q1am_oldnew_action(qa_q1am_oldnew *oldnew, const char *name);
void qa_q1am_oldnew_attack(qa_q1am_oldnew *oldnew);
void qa_q1am_oldnew_finish(qa_q1am_oldnew *oldnew);
qa_q1am_actor *qa_q1am_throw_gib_vector(qa_q1am_context *context, qa_vec3 origin, qa_vec3 direction);
void qa_q1am_oldnew_credits(qa_q1am_context *context);
void qa_q1am_register_oldnew(qa_q1am_context *context);

#endif
