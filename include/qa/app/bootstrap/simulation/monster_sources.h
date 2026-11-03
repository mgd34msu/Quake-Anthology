/* Port of src/app/bootstrap/simulation/monster-sources.ts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_MONSTER_SOURCES_H
#define QA_APP_BOOTSTRAP_SIMULATION_MONSTER_SOURCES_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/foundation/entity_services.h"
#include "qa/content/q1/missionpacks/monsters/runtime.h"

/* The objects TS registerSelectedQ1Expansion creates: the Q1Base it builds
 * over the game, the mission-pack runtime (hipnotic/rogue) and the addon
 * context (dopa/mg1/mg3/ctf). TS keeps them alive through the game's
 * registrations; C owns them here until qa_app_sim_q1_expansion_destroy,
 * which must follow the game's last use. */
typedef struct qa_app_sim_q1_expansion {
  qa_q1_base *base;
  qa_q1mpm_runtime *mission_pack; /* NULL for addon programs */
  qa_q1ad_context *context;       /* TS return value; NULL = TS null */
} qa_app_sim_q1_expansion;

/* TS registerSelectedQ1Expansion. program is the Q1 monster source program
 * other than "id1": "hipnotic" | "rogue" | "dopa" | "mg1" | "mg3" | "ctf". */
qa_app_sim_q1_expansion *qa_app_sim_register_selected_q1_expansion(qa_q1_services *game, const char *program,
                                                                   qa_q1ad_services services);
void qa_app_sim_q1_expansion_destroy(qa_app_sim_q1_expansion *expansion);
#endif
