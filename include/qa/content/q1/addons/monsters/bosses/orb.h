/* Port of src/content/q1/addons/monsters/bosses/orb.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_ORB_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_ORB_H

#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/monsters/ai/controller.h"

/* TS class Q1Orb extends Mg3Monster. */
typedef struct qa_q1am_orb {
  qa_q1am_mg3_monster base;
  qa_q1ad_context *context;
} qa_q1am_orb;

/* TS new Q1Orb(context, entity), in place. False only on OOM. */
bool qa_q1am_orb_init(qa_q1am_orb *orb, qa_q1ad_context *context, qa_q1_actor *entity);
/* instanceof Q1Orb. */
bool qa_q1am_is_orb(const qa_q1_base_monster *monster);
void qa_q1am_orb_action(qa_q1am_orb *orb, const char *name);
void qa_q1am_orb_blast(qa_q1am_orb *orb);
void qa_q1am_orb_death_touch(qa_q1am_orb *orb, qa_actor_id other);
void qa_q1am_register_orb(qa_q1ad_context *context);

#endif
