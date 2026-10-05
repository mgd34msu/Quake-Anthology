#ifndef QA_APPLICATION_NATIVE_Q2_CHECKPOINT_H
#define QA_APPLICATION_NATIVE_Q2_CHECKPOINT_H

#include "qa/common.h"
#include "qa/save.h"

struct application_provider;
bool application_native_q2_checkpoint_capture(struct application_provider *, qa_save_purpose,
    qa_buffer *, qa_error *);
/* A visited level reuses its current unit's Source settings. An ordinary save
 * imports its one saved GAME settings owner before player/policy consumers. */
bool application_native_q2_checkpoint_prepare(struct application_provider *,
    const struct application_provider *current, const qa_save_record *, qa_error *);
bool application_native_q2_checkpoint_restore(struct application_provider *, qa_bytes, qa_error *);

#endif
