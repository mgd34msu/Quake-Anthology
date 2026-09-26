#ifndef QA_ROQ_INTERNAL_H
#define QA_ROQ_INTERNAL_H

#include "qa/binary.h"
#include "qa/media.h"

#define ROQ_FILE_BYTES (65536u + 8u)
#define ROQ_FRAME_BYTES (512u * 512u * 4u * 2u)
#define ROQ_SAVED_BYTES (ROQ_FILE_BYTES + sizeof(qa_roq_codebooks) + ROQ_FRAME_BYTES)

struct qa_roq_scratch {
    size_t references;
    uint8_t file[ROQ_FILE_BYTES];
    qa_roq_codebooks books;
    uint8_t frames[ROQ_FRAME_BYTES];
};
typedef struct roq_stream {
    qa_media_input *input;
    uint8_t *file;
    qa_roq_stream_checkpoint state;
    qa_roq_end_policy policy;
    bool pending;
} roq_stream;
typedef struct roq_chunk {
    uint16_t id, flags;
    qa_bytes bytes;
    bool ended;
} roq_chunk;
bool roq_stream_init(roq_stream *, bool reset, qa_error *);
bool roq_stream_next(roq_stream *, roq_chunk *, qa_error *);
bool roq_stream_complete(roq_stream *, bool ended, qa_error *);
bool roq_fail(qa_error *, const char *);

#endif
