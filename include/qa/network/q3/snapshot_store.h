/* Port of src/network/q3/snapshot-store.ts. GPL-2.0-or-later. */
#ifndef QA_NETWORK_Q3_SNAPSHOT_STORE_H
#define QA_NETWORK_Q3_SNAPSHOT_STORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/q3/message.h"
#include "qa/network/q3/server_message.h"
#include "qa/network/q3/state/entity.h"
#include "qa/network/q3/state/player.h"
typedef struct qa_q3_snapshot_entities qa_q3_snapshot_entities;
bool qa_q3_snapshot_entities_init(qa_q3_snapshot_entities **out, int64_t length);
void qa_q3_snapshot_entities_destroy(qa_q3_snapshot_entities *e);
int64_t qa_q3_snapshot_entities_length(const qa_q3_snapshot_entities *e);
int64_t qa_q3_snapshot_entities_next(const qa_q3_snapshot_entities *e);
void qa_q3_snapshot_entities_set_next(qa_q3_snapshot_entities *e, int64_t next);
void qa_q3_snapshot_entities_append(qa_q3_snapshot_entities *e, const qa_q3_entity_state_record *entity);
void qa_q3_snapshot_entities_read(const qa_q3_snapshot_entities *e, int64_t absolute, qa_q3_entity_state_record *out);
/* TS Q3ServerFrame (mutable). */
typedef struct qa_q3_server_frame {
  qa_q3_player_state_record player_state;
  uint8_t *area_mask; size_t area_mask_len;
  int64_t first_entity, num_entities, message_size, message_sent, message_acked;
} qa_q3_server_frame;
/* baseline closure: fills out (TS returns EntityStateFields, never null here). */
typedef void (*qa_q3_server_baseline_fn)(void *ctx, int64_t number, qa_q3_entity_state_record *out);
typedef struct qa_q3_server_snapshot_history qa_q3_server_snapshot_history;
bool qa_q3_server_snapshot_history_init(qa_q3_server_snapshot_history **out, qa_q3_snapshot_entities *entities,
                                        qa_q3_product product, qa_q3_server_baseline_fn baseline, void *baseline_ctx);
void qa_q3_server_snapshot_history_destroy(qa_q3_server_snapshot_history *h);
qa_q3_server_frame *qa_q3_server_snapshot_history_frames(qa_q3_server_snapshot_history *h, size_t *count);
qa_q3_server_frame *qa_q3_server_snapshot_history_frame(qa_q3_server_snapshot_history *h, int64_t sequence);
qa_q3_server_frame *qa_q3_server_snapshot_history_capture(qa_q3_server_snapshot_history *h, int64_t sequence,
                                                          const qa_q3_player_state_record *player,
                                                          const uint8_t *area_mask, size_t area_mask_len,
                                                          const qa_q3_entity_state_record *entities, size_t entity_count);
int64_t qa_q3_server_snapshot_history_delta(const qa_q3_server_snapshot_history *h, int64_t sequence, int64_t requested, bool active);
void qa_q3_server_snapshot_history_write(qa_q3_server_snapshot_history *h, qa_q3_message_writer *writer,
                                         int64_t sequence, int64_t requested, bool active, int64_t server_time, int64_t flags);
bool qa_q3_server_snapshot_history_copy(qa_q3_server_snapshot_history *h, int64_t sequence, int64_t delta_number,
                                        int64_t server_time, int64_t flags, int64_t server_command_number, qa_q3_wire_snapshot *out);
qa_q3_product qa_q3_server_snapshot_history_get_product(const qa_q3_server_snapshot_history *h);
qa_q3_snapshot_entities *qa_q3_server_snapshot_history_get_entities(const qa_q3_server_snapshot_history *h);
void qa_q3_server_snapshot_history_baseline(const qa_q3_server_snapshot_history *h, int64_t number, qa_q3_entity_state_record *out);
#endif
