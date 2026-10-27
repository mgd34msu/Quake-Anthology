/* Port of src/content/q2/missionpacks/entities/types.ts. GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/entities/types.h"

const char *qa_q2mp_entity_event_kind_name(qa_q2mp_entity_event_kind kind) {
  return kind == QA_Q2MP_ENTITY_EVENT_STEAM ? "steam" : "force-wall";
}
