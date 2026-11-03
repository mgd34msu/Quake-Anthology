/* Port of src/content/composition/q2/types.ts: the program and match-kind
 * spellings and the DeathBall selection reads (plain values or the live
 * cvar getters of match-selection.ts). GPL-2.0-or-later. */
#include "qa/content/composition/q2/types.h"

#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

const char *qa_q2_program_name(qa_q2_program program) {
  static const char *const NAMES[] = {"baseq2", "xatrix", "rogue", "mg2", "n64"};
  return NAMES[program];
}

const char *qa_q2_match_kind_name(qa_q2_match_kind kind) {
  static const char *const NAMES[] = {"ctf", "lmctf", "standard", "tag", "horde", "deathball"};
  return NAMES[kind];
}

static char *copy_text(const char *text) {
  char *copy = strdup(text != NULL ? text : "");
  if (copy == NULL) qa_range_error("out of memory");
  return copy;
}

char *qa_q2_match_selection_team1_skin(const qa_q2_match_selection *selection) {
  if (selection->cvars == NULL) return copy_text(selection->team1_skin);
  char *value = qa_cvar_registry_variable_string(selection->cvars, "dball_team1_skin");
  if (value == NULL) qa_range_error("out of memory");
  return value;
}

char *qa_q2_match_selection_team2_skin(const qa_q2_match_selection *selection) {
  if (selection->cvars == NULL) return copy_text(selection->team2_skin);
  char *value = qa_cvar_registry_variable_string(selection->cvars, "dball_team2_skin");
  if (value == NULL) qa_range_error("out of memory");
  return value;
}

double qa_q2_match_selection_goal_limit(const qa_q2_match_selection *selection) {
  return selection->cvars != NULL ? qa_cvar_registry_variable_value(selection->cvars, "goallimit")
                                  : selection->goal_limit;
}
