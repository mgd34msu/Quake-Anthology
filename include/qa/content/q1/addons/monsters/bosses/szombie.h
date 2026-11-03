/* Port of src/content/q1/addons/monsters/bosses/szombie.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_SZOMBIE_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_SZOMBIE_H

#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/monsters/ai/controller.h"

/* TS class Q1ShubZombie extends Mg3Monster. */
typedef struct qa_q1am_shub_zombie {
  qa_q1am_mg3_monster base;
  qa_q1ad_context *context;
} qa_q1am_shub_zombie;

/* TS new Q1ShubZombie(context, entity), in place. False only on OOM. */
bool qa_q1am_shub_zombie_init(qa_q1am_shub_zombie *zombie, qa_q1ad_context *context, qa_q1_actor *entity);
/* instanceof Q1ShubZombie. */
bool qa_q1am_is_shub_zombie(const qa_q1_base_monster *monster);
void qa_q1am_shub_zombie_action(qa_q1am_shub_zombie *zombie, const char *name);
/* NULL = TS null. */
qa_q1_actor *qa_q1am_spawn_shub_zombie(qa_q1ad_context *context);
void qa_q1am_register_shub_zombie(qa_q1ad_context *context);

#endif
