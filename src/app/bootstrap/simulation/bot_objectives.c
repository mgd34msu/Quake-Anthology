/* Port of src/app/bootstrap/simulation/bot-objectives.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/bot_objectives.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
static bool is(const char *a, const char *b) { return a != NULL && strcmp(a, b) == 0; }
double qa_app_sim_rerelease_bot_team_of(const char *team) {
  if (is(team, "team:red") || is(team, "red") || is(team, "1")) return 1;
  if (is(team, "team:blue") || is(team, "blue") || is(team, "2")) return 2;
  return 0;
}
struct qa_app_sim_rerelease_bot_objectives { const void *simulation; const void *knowledge; };
qa_app_sim_rerelease_bot_objectives *qa_app_sim_rerelease_bot_objectives_create(const void *simulation, const void *knowledge) {
  if (simulation == NULL && knowledge == NULL) qa_range_error("Bot objective source registry unavailable");
  qa_app_sim_rerelease_bot_objectives *o = calloc(1, sizeof *o);
  if (o == NULL) qa_range_error("Out of memory");
  o->simulation = simulation; o->knowledge = knowledge; return o;
}
void qa_app_sim_rerelease_bot_objectives_destroy(qa_app_sim_rerelease_bot_objectives *o) { free(o); }
