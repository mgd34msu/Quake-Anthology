/* Port of src/app/bootstrap/simulation/foreign-q1-world.ts: a full Q1 world
 * (base entities, mission packs, campaign addons, monsters) hosted as an
 * adjunct for a Q1-authored map whose primary source is another family's
 * program. Mirrors the primary Q1 composition; player/match ownership stays
 * with the primary source, so player-facing services route to shared
 * runtime services and no clients attach here. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_FOREIGN_Q1_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_FOREIGN_Q1_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/composition/q1/runtime.h"
#include "qa/content/q1/base/provider.h"
#include "qa/content/q1/foundation/entity_services.h"

/* TS ForeignQ1WorldModules { game, composition }. game is composition.game. */
typedef struct qa_app_sim_foreign_q1_world_modules {
  qa_q1_services *game;
  qa_comp_q1_source_composition composition;
  qa_comp_q1_source_selection selection; /* storage the composition borrows */
} qa_app_sim_foreign_q1_world_modules;

/* TS createForeignQ1WorldModules({ host, foundation, program, campaign,
 * services }). foundation is Omit<Q1FoundationOptions, "precacheProgram">
 * (its precache_id1 is ignored). foundation, campaign and services are
 * borrowed and must outlive the modules. Aborts on OOM. */
qa_app_sim_foreign_q1_world_modules *qa_app_sim_create_foreign_q1_world_modules(qa_q1_host host, const qa_q1_options *foundation,
    qa_comp_q1_program program, const qa_q1_campaign_binding *campaign, const qa_comp_q1_services *services);
void qa_app_sim_foreign_q1_world_modules_destroy(qa_app_sim_foreign_q1_world_modules *modules);
#endif
