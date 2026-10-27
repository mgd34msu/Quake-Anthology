/* Port of src/network/q3/snapshot-history.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_SNAPSHOT_HISTORY_H
#define QA_NETWORK_Q3_SNAPSHOT_HISTORY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/parse_entities.h"
#include "qa/network/q3/server_message.h"
#define QA_Q3_SNAPSHOT_BACKUP 32
/* TS OwnedSnapshot = wire snapshot value. copySnapshot deep-copies. */
bool qa_q3_copy_snapshot(qa_q3_wire_snapshot *dst, const qa_q3_wire_snapshot *src);
typedef struct qa_q3_snapshot_history qa_q3_snapshot_history;
bool qa_q3_snapshot_history_init(qa_q3_snapshot_history **out, qa_q3_source_parse_entities *parse_entities);
void qa_q3_snapshot_history_destroy(qa_q3_snapshot_history *h);
qa_q3_source_parse_entities *qa_q3_snapshot_history_parse_entities(const qa_q3_snapshot_history *h);
/* latest: false = null; out is a deep copy (destroy with qa_q3_wire_snapshot_destroy). */
bool qa_q3_snapshot_history_latest(const qa_q3_snapshot_history *h, qa_q3_wire_snapshot *out);
/* readCurrentPlayerState: false = null. */
bool qa_q3_snapshot_history_read_current_player_state(const qa_q3_snapshot_history *h, qa_q3_player_state_record *out);
/* readSlot: false = null; out deep copy. */
bool qa_q3_snapshot_history_read_slot(const qa_q3_snapshot_history *h, int64_t message_number, bool *valid, qa_q3_wire_snapshot *out);
/* borrowSlot: live retained entry (zero invalid created on demand). The
 * returned entry's snapshot area mask is full 32 bytes like TS zero record. */
qa_q3_snapshot_history_entry *qa_q3_snapshot_history_borrow_slot(qa_q3_snapshot_history *h, int64_t message_number, qa_q3_product product);
/* publish takes a snapshot-kind server operation. */
bool qa_q3_snapshot_history_publish(qa_q3_snapshot_history *h, const qa_q3_server_operation *operation);
void qa_q3_snapshot_history_clear(qa_q3_snapshot_history *h);
#endif
