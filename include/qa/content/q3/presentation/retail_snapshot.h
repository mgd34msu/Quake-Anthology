/* Port of src/content/q3/presentation/retail-snapshot.ts. GPL-2.0-or-later.
 * Retail cgame state from id Software's bg_public.h / q_shared.h.
 * The wire Snapshot is qa/network/q3/server_message.h's qa_q3_wire_snapshot;
 * playerState becomes the shared (base/shared/player-state.ts) record and
 * entities become fresh shared EntityState records, as in TS. */
#ifndef QA_CONTENT_Q3_PRESENTATION_RETAIL_SNAPSHOT_H
#define QA_CONTENT_Q3_PRESENTATION_RETAIL_SNAPSHOT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/content/q3/base/shared/entity_state.h"
#include "qa/content/q3/base/shared/player_state.h"
#include "qa/network/q3/server_message.h"
typedef struct qa_q3_pres_retail_snapshot {
  int64_t message_number, server_time, delta_number, flags, server_command_number, parse_entities_number;
  uint8_t area_mask[32];
  size_t area_mask_length;
  qa_q3_base_source_player_state player_state;
  qa_q3_base_entity_state *entities; /* owned */
  size_t entity_count;
} qa_q3_pres_retail_snapshot;
/* TS retailSnapshot(source). False only on OOM; out is then zeroed. */
bool qa_q3_pres_retail_snapshot_from_wire(qa_q3_pres_retail_snapshot *out, const qa_q3_wire_snapshot *source);
void qa_q3_pres_retail_snapshot_destroy(qa_q3_pres_retail_snapshot *snapshot);
#endif
