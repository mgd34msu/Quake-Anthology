/* Port of src/content/composition/q2/types.ts (types only; name tables + selection getters). GPL-2.0-or-later. */
#include "qa/content/composition/q2/types.h"

#include <stddef.h>

const char *qa_comp_q2_program_name(qa_comp_q2_program program) {
  static const char *const NAMES[] = {"baseq2", "xatrix", "rogue", "mg2", "n64"};
  return NAMES[program];
}

const char *qa_comp_q2_match_kind_name(qa_comp_q2_match_kind kind) {
  static const char *const NAMES[] = {"ctf", "lmctf", "standard", "tag", "horde", "deathball"};
  return NAMES[kind];
}

const char *qa_comp_q2_match_selection_team1_skin(const qa_comp_q2_match_selection *selection, bool *owned) {
  if (selection->cvars != NULL) {
    *owned = true;
    return qa_cvar_registry_variable_string(selection->cvars, "dball_team1_skin");
  }
  *owned = false;
  return selection->team1_skin;
}

const char *qa_comp_q2_match_selection_team2_skin(const qa_comp_q2_match_selection *selection, bool *owned) {
  if (selection->cvars != NULL) {
    *owned = true;
    return qa_cvar_registry_variable_string(selection->cvars, "dball_team2_skin");
  }
  *owned = false;
  return selection->team2_skin;
}

double qa_comp_q2_match_selection_goal_limit(const qa_comp_q2_match_selection *selection) {
  return selection->cvars != NULL ? qa_cvar_registry_variable_value(selection->cvars, "goallimit")
                                   : selection->goal_limit;
}
