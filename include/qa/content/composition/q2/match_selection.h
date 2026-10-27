/* Port of src/content/composition/q2/match-selection.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_COMPOSITION_Q2_MATCH_SELECTION_H
#define QA_CONTENT_COMPOSITION_Q2_MATCH_SELECTION_H

#include <stdbool.h>

#include "qa/content/composition/q2/types.h"

/** TS sourceQ2MatchSelection(); travel may be NULL (TS undefined). */
qa_comp_q2_match_selection qa_comp_source_q2_match_selection(const char *provider, qa_cvar_registry *cvars,
                                                             const qa_comp_lmctf_travel *travel);

#endif
