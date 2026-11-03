/* Port of src/app/bootstrap/simulation/bot-objectives.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_OBJECTIVES_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_OBJECTIVES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/app/bootstrap/simulation/bot_rerelease_world.h"
/* TS team fallback: "team:red"|"red"|"1" -> 1, "team:blue"|"blue"|"2" -> 2, else 0. */
double qa_app_sim_rerelease_bot_team_of(const char *team);
/* TS rereleaseBotObjectives(simulation, knowledge): objectives.ctx is the
 * returned struct (heap; free with destroy). Raises when no registry exists. */
typedef struct qa_app_sim_rerelease_bot_objectives_state {
  qa_app_sim_rerelease_bot_objectives objectives;
  const qa_app_sim_bot_simulation *simulation;
  const qa_bot_knowledge *knowledge;
  qa_cvar_registry *registry;
} qa_app_sim_rerelease_bot_objectives_state;
qa_app_sim_rerelease_bot_objectives_state *qa_app_sim_rerelease_bot_objectives_create(const qa_app_sim_bot_simulation *simulation,
    const qa_bot_knowledge *knowledge);
void qa_app_sim_rerelease_bot_objectives_destroy(qa_app_sim_rerelease_bot_objectives_state *objectives);
#endif
