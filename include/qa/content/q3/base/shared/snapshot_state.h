/* Port of src/content/q3/base/shared/snapshot-state.ts. GPL-2.0-or-later.
 * Destination is the landed network EntityState record (int32 storage);
 * TS numeric assignments into it narrow by its storage rules. */
#ifndef QA_CONTENT_Q3_BASE_SHARED_SNAPSHOT_STATE_H
#define QA_CONTENT_Q3_BASE_SHARED_SNAPSHOT_STATE_H
#include <stdbool.h>
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/player_state.h"
void qa_q3_base_player_state_to_entity_state(qa_q3_base_source_player_state *ps,
    qa_q3_entity_state_record *destination, bool snap);
void qa_q3_base_player_state_to_entity_state_extrapolate(qa_q3_base_source_player_state *ps,
    qa_q3_entity_state_record *destination, double time, bool snap);
#endif
