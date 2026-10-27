/* Port of src/content/q2/rerelease/fog.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_FOG_H
#define QA_CONTENT_Q2_RERELEASE_FOG_H
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/types.h"
qa_q2_fog_state qa_q2_rerelease_fog_fields(const qa_q2_spawn_fields *fields, bool off);
qa_q2_fog_state qa_q2_interpolate_fog(qa_q2_fog_state off, qa_q2_fog_state on, double fraction);
bool qa_q2_equal_fog(qa_q2_fog_state a, qa_q2_fog_state b);
#endif
