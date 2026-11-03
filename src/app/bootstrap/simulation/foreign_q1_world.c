/* Port of src/app/bootstrap/simulation/foreign-q1-world.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/foreign_q1_world.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

qa_app_sim_foreign_q1_world_modules *qa_app_sim_create_foreign_q1_world_modules(qa_q1_host host, const qa_q1_options *foundation,
    qa_comp_q1_program program, const qa_q1_campaign_binding *campaign, const qa_comp_q1_services *services) {
  qa_app_sim_foreign_q1_world_modules *modules = calloc(1, sizeof *modules);
  if (!modules) qa_range_error("Out of memory");
  /* { program, campaign, registered: true, officialCampaign: program === "id1" } */
  memset(&modules->selection, 0, sizeof modules->selection);
  modules->selection.program = program;
  modules->selection.campaign = *campaign;
  modules->selection.registered = true;
  modules->selection.official_campaign = program == QA_COMP_Q1_ID1;
  if (!qa_comp_create_q1_source_composition(&modules->composition, host, foundation, &modules->selection, services))
    qa_range_error("Out of memory");
  modules->game = modules->composition.game;
  return modules;
}
void qa_app_sim_foreign_q1_world_modules_destroy(qa_app_sim_foreign_q1_world_modules *modules) {
  if (!modules) return;
  qa_comp_q1_source_composition_destroy(&modules->composition);
  free(modules);
}
