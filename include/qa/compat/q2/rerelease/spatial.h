/* Port of src/compat/q2/rerelease/spatial.ts. GPL-2.0-or-later. */
#ifndef QA_COMPAT_Q2_RERELEASE_SPATIAL_H
#define QA_COMPAT_Q2_RERELEASE_SPATIAL_H
#include "qa/contracts/world.h"

qa_bounds qa_compat_q2_rerelease_link_bounds(qa_body_state state, double solid);
double qa_compat_q2_rerelease_network_solid(qa_bounds bounds, double solid, double server_flags);
#endif
