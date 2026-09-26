#ifndef QA_NETWORK_Q2_BATCH_H
#define QA_NETWORK_Q2_BATCH_H
#include "qa/network_q2.h"

#define QA_Q2PRO_BATCH_FRAMES 3u
#define QA_Q2PRO_BATCH_COMMANDS 31u
typedef struct qa_q2pro_batch_frame {
    size_t count;
    qa_q2_usercmd commands[QA_Q2PRO_BATCH_COMMANDS];
} qa_q2pro_batch_frame;
typedef struct qa_q2pro_batch {
    bool nodelta;
    int32_t lastframe;
    uint8_t lightlevel;
    size_t frame_count;
    qa_q2pro_batch_frame frames[QA_Q2PRO_BATCH_FRAMES];
} qa_q2pro_batch;
/* Read/write the body after opcode 10/11. opcode_extra is raw_opcode >> 5.
 * The final bit byte is consumed/padded; the following command is byte aligned.
 * The donor carries lightlevel once per batch, separate from command deltas. */
bool qa_q2pro_read_batch(qa_q2_codec *, qa_net_reader *, bool nodelta,
                         uint8_t opcode_extra, qa_q2pro_batch *);
bool qa_q2pro_write_batch(qa_q2_codec *, qa_net_writer *, const qa_q2pro_batch *);
bool qa_q2pro_write_batch_message(qa_q2_codec *, qa_net_writer *, const qa_q2pro_batch *);
bool qa_q2pro_read_userinfo_delta(qa_net_reader *, char *name, size_t name_capacity,
                                  char *value, size_t value_capacity);
bool qa_q2pro_write_userinfo_delta(qa_net_writer *, const char *name, const char *value);
bool qa_q2pro_read_client_setting(qa_net_reader *, int16_t *index, int16_t *value);
bool qa_q2pro_write_client_setting(qa_net_writer *, int16_t index, int16_t value);
/* Explicit extended wire scalars, also used by original record consumers. */
bool qa_q2pro_read_int23(qa_net_reader *, int32_t previous, int32_t *);
bool qa_q2pro_write_int23(qa_net_writer *, int32_t current, int32_t previous);
bool qa_q2pro_read_var64(qa_net_reader *, uint64_t *);
bool qa_q2pro_write_var64(qa_net_writer *, uint64_t);
bool qa_q2pro_read_fog(qa_net_reader *, const qa_q2_player_fog *, qa_q2_player_fog *);
uint8_t qa_q2pro_fog_bits(const qa_q2_player_fog *, const qa_q2_player_fog *);
bool qa_q2pro_write_fog(qa_net_writer *, uint8_t, const qa_q2_player_fog *);
#endif
