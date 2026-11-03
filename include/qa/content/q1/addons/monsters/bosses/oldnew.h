/* Port of src/content/q1/addons/monsters/bosses/oldnew.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_OLDNEW_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_OLDNEW_H

#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/monsters/ai/controller.h"

/* TS class Q1Oldnew extends Mg3Monster. */
typedef struct qa_q1am_oldnew {
  qa_q1am_mg3_monster base;
  qa_q1ad_context *context;
} qa_q1am_oldnew;

/* TS new Q1Oldnew(context, entity), in place. False only on OOM. */
bool qa_q1am_oldnew_init(qa_q1am_oldnew *oldnew, qa_q1ad_context *context, qa_q1_actor *entity);
/* instanceof Q1Oldnew. */
bool qa_q1am_is_oldnew(const qa_q1_base_monster *monster);
void qa_q1am_oldnew_attack(qa_q1am_oldnew *oldnew);
void qa_q1am_oldnew_action(qa_q1am_oldnew *oldnew, const char *name);
void qa_q1am_oldnew_finish(qa_q1am_oldnew *oldnew);
qa_q1_actor *qa_q1am_throw_gib_vector(qa_q1ad_context *context, qa_vec3 origin, qa_vec3 direction);
void qa_q1am_oldnew_credits(qa_q1ad_context *context);
void qa_q1am_register_oldnew(qa_q1ad_context *context);

#endif
