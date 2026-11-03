/* Port of src/app/bootstrap/simulation/monster-sources.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_sources.h"
#include <stdlib.h>
#include <string.h>
#include "qa/content/q1/addons/monsters/index.h"
#include "qa/content/q1/missionpacks/monsters.h"
#include "qa/foundation/support.h"

static qa_q1ad_program addon_program(const char *program) {
  if (strcmp(program, "dopa") == 0) return QA_Q1AD_DOPA;
  if (strcmp(program, "mg1") == 0) return QA_Q1AD_MG1;
  if (strcmp(program, "mg3") == 0) return QA_Q1AD_MG3;
  if (strcmp(program, "ctf") == 0) return QA_Q1AD_CTF;
  qa_range_error("Unknown selected Q1 expansion program");
}

qa_app_sim_q1_expansion *qa_app_sim_register_selected_q1_expansion(qa_q1_services *game, const char *program,
                                                                   qa_q1ad_services services) {
  qa_app_sim_q1_expansion *expansion = calloc(1, sizeof *expansion);
  if (expansion == NULL) qa_range_error("Out of memory");
  qa_q1_base_options options;
  memset(&options, 0, sizeof options);
  options.has_official_campaign = true;
  options.official_campaign = false;
  expansion->base = qa_q1_base_create(game, &options);
  if (expansion->base == NULL) qa_range_error("Out of memory");
  if (strcmp(program, "hipnotic") == 0 || strcmp(program, "rogue") == 0) {
    expansion->mission_pack = calloc(1, sizeof *expansion->mission_pack);
    if (expansion->mission_pack == NULL) qa_range_error("Out of memory");
    if (qa_q1mpm_register_mission_pack_monsters(game, expansion->base, program, NULL, expansion->mission_pack) == NULL)
      qa_range_error("Out of memory");
  } else {
    expansion->context = qa_q1ad_context_create(expansion->base, addon_program(program), services);
    if (expansion->context == NULL) qa_range_error("Out of memory");
    qa_q1am_register_addon_monsters(expansion->context);
  }
  return expansion;
}

void qa_app_sim_q1_expansion_destroy(qa_app_sim_q1_expansion *expansion) {
  if (expansion == NULL) return;
  if (expansion->context != NULL) qa_q1ad_context_destroy(expansion->context);
  if (expansion->mission_pack != NULL) {
    qa_q1mpm_runtime_destroy(expansion->mission_pack);
    free(expansion->mission_pack);
  }
  qa_q1_base_destroy(expansion->base);
  free(expansion);
}
