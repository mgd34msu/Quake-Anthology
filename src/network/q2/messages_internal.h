#ifndef QA_Q2_MESSAGES_INTERNAL_H
#define QA_Q2_MESSAGES_INTERNAL_H
#include "qa/network_q2_messages.h"
#include <zlib.h>
typedef struct qa_q2_inflate_segment {
    struct qa_q2_inflate_segment *next;
    qa_buffer compressed;
    size_t output_size;
} qa_q2_inflate_segment;
struct qa_q2_messages {
    qa_q2_codec codec;
    qa_q2_message_options options;
    char **configs;
    int16_t *inventory;
    size_t config_capacity;
    qa_q2_entity *baselines;
    size_t baseline_count, baseline_capacity;
    qa_q2_frame_history *histories[QA_Q2_MAX_SEATS];
    uint8_t seat;
    enum { STREAM_NONE, STREAM_CONFIG, STREAM_BASELINE, STREAM_GAMESTATE } stream;
    z_stream download;
    bool download_open, reading;
    qa_q2_inflate_segment *download_first, *download_last;
    size_t inflated_this_read;
};
/* Restores codec-private deflate state from the genuinely accepted compressed
 * segment receipts. It emits no service record or application callback. */
bool qa_q2_messages_restore_download_segment(qa_q2_messages *, qa_bytes, size_t output_size, qa_error *);
#endif
