/* Port of src/app/bootstrap/simulation/monster-sources.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MONSTER_SOURCES_H
#define QA_APP_BOOTSTRAP_SIMULATION_MONSTER_SOURCES_H
#include <stdbool.h>
#include <stddef.h>
/* TS registerSelectedQ1Expansion returns context only for addon programs (not hipnotic/rogue). */
bool qa_app_sim_q1_expansion_returns_context(const char *program);
#endif
