/* Port of src/content/composition/q2/match-selection.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2/match_selection.h"

#include <string.h>

qa_comp_q2_match_selection qa_comp_source_q2_match_selection(const char *provider, qa_cvar_registry *cvars,
                                                             const qa_comp_lmctf_travel *travel) {
  qa_comp_q2_match_selection selection = {0};
  if (strcmp(provider, "q2:lmctf") == 0) {
    selection.kind = QA_COMP_Q2_MATCH_LMCTF;
    selection.has_travel = travel != NULL;
    selection.travel = travel;
    return selection;
  }
  if (strcmp(provider, "q2:ctf") == 0) { selection.kind = QA_COMP_Q2_MATCH_CTF; return selection; }
  if (strcmp(provider, "q2:tag") == 0) { selection.kind = QA_COMP_Q2_MATCH_TAG; return selection; }
  if (strcmp(provider, "q2:horde") == 0) { selection.kind = QA_COMP_Q2_MATCH_HORDE; return selection; }
  if (strcmp(provider, "q2:deathball") == 0) {
    selection.kind = QA_COMP_Q2_MATCH_DEATHBALL;
    selection.cvars = cvars; /* live getters: dball_team1_skin/_team2_/goallimit */
    return selection;
  }
  selection.kind = QA_COMP_Q2_MATCH_STANDARD;
  return selection;
}
