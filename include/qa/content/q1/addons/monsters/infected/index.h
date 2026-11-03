/* Port of src/content/q1/addons/monsters/infected/index.ts.
 * quakec_mg3/monsters/mg3_*_infected.qc. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_INDEX_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_INDEX_H

#include "qa/content/q1/addons/monsters/infected/controller.h"

/* TS registerMg3Infected(context): ReadonlyMap<OwnedActor, Q1Infected>. */
typedef struct qa_q1am_infected_registry qa_q1am_infected_registry;
qa_q1am_infected_registry *qa_q1am_register_mg3_infected(qa_q1ad_context *context);
qa_q1am_infected *qa_q1am_infected_registry_get(const qa_q1am_infected_registry *monsters, qa_owned_actor actor);
/* C ownership (TS GC): frees the game's infected registries. */
void qa_q1am_release_mg3_infected(qa_q1_services *game);

#endif
