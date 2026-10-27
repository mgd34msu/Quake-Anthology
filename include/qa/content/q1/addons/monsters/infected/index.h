/* Port of src/content/q1/addons/monsters/infected/index.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_INDEX_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_INDEX_H

#include "qa/content/q1/addons/monsters/infected/controller.h"

/* Returns the registry head (TS ReadonlyMap values in insertion order). */
qa_q1am_infected *qa_q1am_register_mg3_infected(qa_q1am_context *context);

#endif
