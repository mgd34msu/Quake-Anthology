/* Port of src/content/q1/addons/monsters/infected/controller.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_CONTROLLER_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_CONTROLLER_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

#define QA_Q1AM_INFECTED_PREFIX "mg3:infected"

typedef struct qa_q1am_infected qa_q1am_infected;
typedef struct qa_q1am_infected_replace {
  void *ctx;
  qa_q1am_infected *(*fn)(void *ctx, qa_q1am_actor *entity);
} qa_q1am_infected_replace;
struct qa_q1am_infected {
  qa_q1am_monster base;
  qa_q1am_monster_ops super_ops;
  qa_q1am_context *context;
  qa_q1am_infected_replace replace;
  struct qa_q1am_infected *next;
};

void qa_q1am_infected_init(qa_q1am_infected *infected, qa_q1am_context *context,
                           qa_q1am_actor *entity, qa_q1am_infected_replace replace);
void qa_q1am_infected_retarget(qa_q1am_infected *infected);
void qa_q1am_infected_resurrect(qa_q1am_infected *infected);

#endif
