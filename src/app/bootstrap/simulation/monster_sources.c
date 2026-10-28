/* Port of src/app/bootstrap/simulation/monster-sources.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/monster_sources.h"
#include <string.h>
bool qa_app_sim_q1_expansion_returns_context(const char *program) {
  return !(program != NULL && (strcmp(program, "hipnotic") == 0 || strcmp(program, "rogue") == 0));
}
