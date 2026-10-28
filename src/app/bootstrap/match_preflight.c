/* Port of src/app/bootstrap/match-preflight.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/match_preflight.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/match_modes.h"
#include "qa/content/catalog/source_program.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/foundation/support.h"
static bool same_ref(qa_provider_reference a, qa_provider_reference b) {
  return strcmp(a.content, b.content) == 0 && strcmp(a.provider, b.provider) == 0;
}
static bool ends_with(const char *text, const char *suffix) {
  size_t a = strlen(text), b = strlen(suffix);
  return a >= b && strcmp(text + a - b, suffix) == 0;
}
void qa_app_preflight_application_match(qa_app_preflight_content content, const char *mode,
    const qa_app_source_value *source_values, size_t source_value_count) {
  const qa_executable_recipe *recipe = content.recipe;
  qa_provider_reference owner = recipe->map.entities;
  const qa_execution_module *execution = NULL;
  for (size_t i = 0; i < recipe->execution_count; i++) {
    const qa_execution_module *module = &recipe->execution[i];
    if (strcmp(module->role, "server-game") == 0 && same_ref(module->owner, owner)) { execution = module; break; }
  }
  if (execution == NULL || execution->kind != QA_EXECUTION_MODULE_TYPESCRIPT) return;
  const qa_catalog_product_expectation *source =
      &qa_source_program_product(content.catalog, owner.content)->expectation;
  if (source->family == QA_GAME_FAMILY_Q3) {
    const char *configured = NULL;
    for (size_t i = 0; i < source_value_count; i++)
      if (strcmp(source_values[i].name, "g_gametype") == 0) configured = source_values[i].value;
    double game_type = configured == NULL
        ? (strcmp(recipe->match.provider, "q3:ctf") == 0 ? QA_Q3_BASE_GT_CTF
           : strcmp(mode, "singleplayer") == 0 ? 2 : 0)
        : strtod(configured, NULL); /* TS Number(configured) */
    bool found = false;
    char *program = qa_selected_source_program(recipe->execution, recipe->execution_count, recipe->map.entities, &found);
    qa_q3_base_product product = found && program != NULL && strcmp(program, "missionpack") == 0
        ? QA_Q3_BASE_PRODUCT_MISSIONPACK : QA_Q3_BASE_PRODUCT_BASEQ3;
    qa_q3_ta_foreign_objective_adaptation result = qa_q3_ta_adapt_foreign_q3_objectives(
        content.world_kind, content.world_entities, product, game_type, NULL, 0);
    free(program);
    if (result.kind == QA_Q3_TA_OBJECTIVE_MISSING) {
      char joined[256] = "";
      for (size_t i = 0; i < result.missing_count; i++) {
        if (i != 0) strncat(joined, ", ", sizeof joined - strlen(joined) - 1);
        strncat(joined, result.missing[i], sizeof joined - strlen(joined) - 1);
      }
      qa_q3_ta_foreign_objective_adaptation_destroy(&result);
      qa_range_error("Selected Q3 match map is missing: %s", joined);
    }
    if (result.kind == QA_Q3_TA_OBJECTIVE_UNSUPPORTED) {
      double bad = result.game_type;
      qa_q3_ta_foreign_objective_adaptation_destroy(&result);
      if (bad == (double)(long long)bad)
        qa_range_error("Selected Q3 product does not support game type %lld", (long long)bad);
      qa_range_error("Selected Q3 product does not support game type %g", bad);
    }
    qa_q3_ta_foreign_objective_adaptation_destroy(&result);
    return;
  }
  const char *provider = recipe->match.provider;
  qa_app_match_rules rules = strcmp(provider, "q1:ctf") == 0 || strcmp(provider, "q2:ctf") == 0 ? "ctf"
      : strcmp(provider, "q2:lmctf") == 0 ? "lmctf" : strcmp(provider, "q2:tag") == 0 ? "tag"
      : strcmp(provider, "q2:deathball") == 0 ? "deathball" : ends_with(provider, ":horde") ? "horde" : "standard";
  size_t entity_count = 0;
  qa_q1_entity *entities = qa_q1_parse_entities(content.world_entities, &entity_count);
  const char **classnames = malloc((entity_count == 0 ? 1 : entity_count) * sizeof *classnames);
  if (classnames == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < entity_count; i++) {
    const char *value = qa_q1_entity_value(&entities[i], "classname");
    classnames[i] = value != NULL ? value : "";
  }
  qa_app_match_mode_selection selection = {source->family, source->edition, source->campaign, mode, rules};
  char scratch[512];
  const char *reason = qa_app_match_map_unavailable(selection, classnames, entity_count, scratch, sizeof scratch);
  char message[512];
  bool unavailable = reason != NULL;
  if (unavailable) snprintf(message, sizeof message, "%s", reason);
  free(classnames);
  qa_q1_entities_destroy(entities, entity_count);
  if (unavailable) qa_range_error("%s", message);
}
