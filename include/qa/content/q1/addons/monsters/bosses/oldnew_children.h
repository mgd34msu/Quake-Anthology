/* Port of src/content/q1/addons/monsters/bosses/oldnew-children.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_OLDNEW_CHILDREN_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_OLDNEW_CHILDREN_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

qa_q1am_actor *qa_q1am_spawn_spammer(qa_q1am_context *context, qa_q1am_actor *owner);
qa_q1am_actor *qa_q1am_spawn_swiper(qa_q1am_context *context, qa_q1am_actor *owner);
qa_q1am_actor *qa_q1am_spawn_eye(qa_q1am_context *context, qa_q1am_actor *owner);
void qa_q1am_spawn_blaster(qa_q1am_context *context, qa_q1am_actor *owner);
qa_q1am_actor *qa_q1am_spawn_vortex(qa_q1am_context *context, qa_q1am_actor *owner);
void qa_q1am_cleanup_oldnew(qa_q1am_context *context);
void qa_q1am_register_oldnew_children(qa_q1am_context *context);

#endif
