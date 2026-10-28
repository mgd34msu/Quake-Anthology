/* Port of src/app/bootstrap/simulation/source-transition.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/source_transition.h"
#include <string.h>
qa_transition_intent qa_app_sim_source_level_transition(qa_transition_intent intent, qa_executable_recipe recipe, qa_app_sim_source_level_authority authority) {
  if (authority != NULL && strcmp(authority, "primary-world") == 0
      && !recipe.campaign.is_campaign /* TS campaign.kind === "none" */
      && intent.kind != NULL && strcmp(intent.kind, "campaign-level") == 0) {
    qa_transition_intent out = intent;
    out.kind = "match-rotation";
    out.match = recipe.match.provider;
    return out;
  }
  return intent;
}
