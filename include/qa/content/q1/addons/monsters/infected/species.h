/* Port of src/content/q1/addons/monsters/infected/species.ts.
 * quakec_mg3/monsters/mg3_*_infected.qc. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_SPECIES_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_INFECTED_SPECIES_H

#include "qa/content/q1/base/species.h"

/* TS InfectedKind "army" | "knight" | "enforcer" | "hellknight" (exact strings). */
typedef struct qa_q1am_infected_classname { const char *classname, *kind; } qa_q1am_infected_classname;
extern const qa_q1am_infected_classname qa_q1am_infected_classnames[]; /* TS infectedClassnames */
extern const size_t qa_q1am_infected_classname_count;
const char *qa_q1am_infected_kind(const qa_q1_actor *entity);
/* TS infectedSpecies(entity), written into *out. */
void qa_q1am_infected_species(const qa_q1_actor *entity, qa_q1_base_monster_species *out);

#endif
