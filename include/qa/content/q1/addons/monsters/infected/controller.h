/* Port of src/content/q1/addons/monsters/infected/controller.ts.
 * quakec_mg3/monsters/mg3_*_infected.qc and monsters.qc. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_CONTROLLER_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_CONTROLLER_H

#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/addons/monsters/ai/controller.h"

#define QA_Q1AM_INFECTED_PREFIX "mg3:infected" /* TS infectedPrefix */

typedef struct qa_q1am_infected qa_q1am_infected;
/* TS replace: (entity: Q1Actor) => Q1Infected. */
typedef qa_q1am_infected *(*qa_q1am_infected_replace_fn)(void *ctx, qa_q1_actor *entity);

/* TS class Q1Infected extends Mg3Monster. */
struct qa_q1am_infected {
  qa_q1am_mg3_monster base;
  qa_q1ad_context *context;
  qa_q1am_infected_replace_fn replace;
  void *replace_ctx;
  qa_q1_base_monster_species spec; /* TS infectedSpecies(entity) at construction */
};

/* TS new Q1Infected(context, entity, replace), in place. False only on OOM. */
bool qa_q1am_infected_init(qa_q1am_infected *infected, qa_q1ad_context *context, qa_q1_actor *entity,
                           qa_q1am_infected_replace_fn replace, void *replace_ctx);
/* instanceof Q1Infected. */
bool qa_q1am_is_infected(const qa_q1_base_monster *monster);
void qa_q1am_infected_retarget(qa_q1am_infected *infected);
void qa_q1am_infected_resurrect(qa_q1am_infected *infected);

#endif
