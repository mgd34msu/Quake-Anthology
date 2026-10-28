/* Port of src/app/bootstrap/simulation/bot-objectives.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_BOT_OBJECTIVES_H
#define QA_APP_BOOTSTRAP_SIMULATION_BOT_OBJECTIVES_H
#include <stdbool.h>
#include <stddef.h>
/* TS team fallback: "team:red"|"red"|"1" -> 1, "team:blue"|"blue"|"2" -> 2, else 0. */
double qa_app_sim_rerelease_bot_team_of(const char *team);
typedef struct qa_app_sim_rerelease_bot_objectives qa_app_sim_rerelease_bot_objectives;
qa_app_sim_rerelease_bot_objectives *qa_app_sim_rerelease_bot_objectives_create(const void *simulation, const void *knowledge);
void qa_app_sim_rerelease_bot_objectives_destroy(qa_app_sim_rerelease_bot_objectives *o);
#endif
