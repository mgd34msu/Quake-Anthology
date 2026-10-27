/* Port of src/compat/qc/message-effects.ts. GPL-2.0-or-later. TS Error throws abort via qa_range_error. */
#ifndef QA_COMPAT_QC_MESSAGE_EFFECTS_H
#define QA_COMPAT_QC_MESSAGE_EFFECTS_H
#include <stdbool.h>
#include "qa/content/q1/foundation/types.h"
#include "qa/network/q1/netquake.h"
qa_q1_event qa_compat_qc_quake_temporary_event(qa_temporary_entity effect, const qa_actor_id *actor, bool quakeworld);
#endif
