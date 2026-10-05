#ifndef QA_FRONTEND_REMOTE_UNIFIED_INPUT_H
#define QA_FRONTEND_REMOTE_UNIFIED_INPUT_H
#include "remote_unified_prediction.h"
#include "qa/input.h"

typedef struct frontend_unified_input frontend_unified_input;
bool frontend_unified_input_create(qa_frontend *, frontend_remote_unified *,
    frontend_remote_unified_prediction *, frontend_unified_input **, qa_error *);
bool frontend_unified_input_build(frontend_unified_input *,
    const qa_seat_input_sample *, uint64_t sequence, double source_elapsed_ms, qa_error *);
/* Retry before sampling; also resumes the physical sequence from the actual
 * completed server acknowledgement when it is ahead of the local producer. */
bool frontend_unified_input_retry(frontend_unified_input *, bool *completed,
    uint64_t *sequence, qa_error *);
bool frontend_unified_input_command_values(frontend_unified_input *,int32_t weapon,float sensitivity,qa_error *);
bool frontend_unified_input_oldest_q3(const frontend_unified_input *,qa_movement_command *,bool *available,qa_error *);
bool frontend_unified_input_idle(const frontend_unified_input *);
bool frontend_unified_input_pending(const frontend_unified_input *);
bool frontend_unified_input_destroy(frontend_unified_input **, qa_error *);

/* The frame owner calls prepare before sampling. A retry may advance the
 * physical sequence without reading the physical input again. */
bool frontend_remote_unified_input_prepare(qa_frontend *, uint32_t physical,
    uint64_t *sequence, bool *owned, bool *sample_needed, qa_error *);
bool frontend_remote_unified_input(qa_frontend *, uint32_t physical,
    const qa_seat_input_sample *, uint64_t sequence, double source_elapsed_ms, bool *handled, qa_error *);
#endif
