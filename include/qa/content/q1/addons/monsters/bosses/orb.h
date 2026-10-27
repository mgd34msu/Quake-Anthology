/* Port of src/content/q1/addons/monsters/bosses/orb.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_ORB_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_BOSSES_ORB_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

typedef struct qa_q1am_orb {
  qa_q1am_monster base;
  qa_q1am_monster_ops super_ops;
  qa_q1am_context *context;
} qa_q1am_orb;

void qa_q1am_orb_init(qa_q1am_orb *orb, qa_q1am_context *context, qa_q1am_actor *entity);
void qa_q1am_orb_action(qa_q1am_orb *orb, const char *name);
void qa_q1am_orb_blast(qa_q1am_orb *orb);
void qa_q1am_orb_death_touch(qa_q1am_orb *orb, qa_actor_id other);
void qa_q1am_register_orb(qa_q1am_context *context);
void qa_q1am_launch_orb_missile(qa_q1am_monster *monster, qa_q1am_actor *missile,
                                double speed, double accuracy);

#endif
