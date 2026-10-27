/* Port of src/content/q1/addons/monsters/infected/species.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_SPECIES_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_SPECIES_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

typedef struct qa_q1am_infected_classname { const char *classname, *kind; } qa_q1am_infected_classname;
extern const qa_q1am_infected_classname qa_q1am_infected_classnames[];
extern const size_t qa_q1am_infected_classname_count;
const char *qa_q1am_infected_kind(qa_q1am_actor *entity);
qa_q1am_species qa_q1am_infected_species(qa_q1am_actor *entity);

#endif
