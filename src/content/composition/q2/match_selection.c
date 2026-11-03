/* Port of src/content/composition/q2/match-selection.ts. GPL-2.0-or-later. */
#include "qa/content/composition/q2/match_selection.h"

#include <string.h>

qa_q2_match_selection qa_source_q2_match_selection(const char *provider, qa_cvar_registry *cvars,
                                                   const qa_lmctf_travel *travel) {
  qa_q2_match_selection selection;
  memset(&selection, 0, sizeof selection);
  selection.kind = QA_Q2_MATCH_STANDARD;
  if (strcmp(provider, "q2:lmctf") == 0) {
    selection.kind = QA_Q2_MATCH_LMCTF;
    selection.travel = travel;
  } else if (strcmp(provider, "q2:ctf") == 0) {
    selection.kind = QA_Q2_MATCH_CTF;
  } else if (strcmp(provider, "q2:tag") == 0) {
    selection.kind = QA_Q2_MATCH_TAG;
  } else if (strcmp(provider, "q2:horde") == 0) {
    selection.kind = QA_Q2_MATCH_HORDE;
  } else if (strcmp(provider, "q2:deathball") == 0) {
    selection.kind = QA_Q2_MATCH_DEATHBALL;
    selection.cvars = cvars;
  }
  return selection;
}
