#ifndef QA_NETWORK_QW_SOURCE_H
#define QA_NETWORK_QW_SOURCE_H

#include "qa/network_q1_qw.h"

/* Widened classic Source integer fields precede wire byte conversion. */
typedef struct qa_qw_source_entity {
    uint32_t number;
    double model, frame, colormap, skin, effects;
    float origin[3], angles[3];
    bool solid;
} qa_qw_source_entity;
typedef struct qa_qw_source_frame {
    uint32_t sequence;
    size_t count;
    qa_qw_source_entity entities[QA_QW_MAX_PACKET_ENTITIES];
} qa_qw_source_frame;
typedef struct qa_qw_source_player {
    uint8_t slot, msec;
    uint16_t flags;
    float origin[3], velocity[3];
    double frame, model, skin, effects, weapon_frame;
    qa_qw_command command;
} qa_qw_source_player;
typedef struct qa_qw_source_history qa_qw_source_history;

qa_qw_source_history *qa_qw_source_history_create(qa_error *);
void qa_qw_source_history_destroy(qa_qw_source_history *);
void qa_qw_source_history_reset(qa_qw_source_history *);
/* Atomically replace genuine source baselines and retire transmitted frames. */
bool qa_qw_source_history_baselines(qa_qw_source_history *,
    const qa_qw_source_entity *, size_t, qa_error *);
const qa_qw_source_frame *qa_qw_source_history_frame(const qa_qw_source_history *, uint32_t);
/* Call only after the complete frame has actually reached its channel packet. */
bool qa_qw_source_history_store(qa_qw_source_history *, const qa_qw_source_frame *, qa_error *);
bool qa_qw_source_history_cut(const qa_qw_source_history *, uint32_t next_outgoing, qa_error *);
bool qa_qw_source_history_checkpoint(const qa_qw_source_history *, uint32_t next_outgoing,
    qa_buffer *, qa_error *);
bool qa_qw_source_history_restore(qa_bytes, uint32_t next_outgoing,
    qa_qw_source_history **empty, qa_error *);

/* Writers retain source opcode/presence/delta decisions before narrowing.
 * No writer changes history. These functions emit complete service records. */
bool qa_qw_source_write_baseline(qa_net_writer *, const qa_qw_source_entity *);
bool qa_qw_source_write_stat(qa_net_writer *, uint8_t index, double truncated_value);
bool qa_qw_source_write_player(qa_net_writer *, const qa_qw_source_player *);
/* Pack actual Source float vectors through native integer shifts and masks.
 * The physical publisher owns its source projectile limit. */
bool qa_qw_source_write_nails(qa_net_writer *, const qa_qw_nail *, size_t count);
bool qa_qw_source_write_entities(qa_net_writer *, const qa_qw_source_history *,
    const qa_qw_source_frame *, const qa_qw_source_frame *previous);

#endif
