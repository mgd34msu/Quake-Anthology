/* Port of src/content/composition/q2/match-selection.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_COMPOSITION_Q2_MATCH_SELECTION_H
#define QA_CONTENT_COMPOSITION_Q2_MATCH_SELECTION_H

#include "qa/content/composition/q2/types.h"

/** TS sourceQ2MatchSelection(provider, cvars, travel?). travel NULL = TS
 * undefined (borrowed). A DeathBall selection keeps cvars for its live
 * getters, so cvars must outlive it. */
qa_q2_match_selection qa_source_q2_match_selection(const char *provider, qa_cvar_registry *cvars,
                                                   const qa_lmctf_travel *travel);

#endif
