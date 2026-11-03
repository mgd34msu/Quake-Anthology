/* Port of src/content/q1/addons/monsters/bosses/ghost.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_GHOST_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_GHOST_H

#include "qa/content/q1/addons/context.h"

/* TS class Q1Ghost extends BaseMonster. */
typedef struct qa_q1am_ghost {
  qa_q1_base_monster base;
  qa_q1ad_context *context;
} qa_q1am_ghost;

/* TS new Q1Ghost(context, entity), in place. False only on OOM. */
bool qa_q1am_ghost_init(qa_q1am_ghost *ghost, qa_q1ad_context *context, qa_q1_actor *entity);
/* instanceof Q1Ghost. */
bool qa_q1am_is_ghost(const qa_q1_base_monster *monster);
void qa_q1am_ghost_action(qa_q1am_ghost *ghost, const char *name);
void qa_q1am_ghost_touch(qa_q1am_ghost *ghost, qa_actor_id other);
void qa_q1am_register_ghost(qa_q1ad_context *context);

#endif
