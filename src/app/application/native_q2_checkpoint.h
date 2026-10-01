#ifndef QA_APPLICATION_NATIVE_Q2_CHECKPOINT_H
#define QA_APPLICATION_NATIVE_Q2_CHECKPOINT_H

#include "qa/common.h"
#include "qa/save.h"

struct application_provider;
bool application_native_q2_checkpoint_capture(struct application_provider *, qa_buffer *, qa_error *);
/* Import the physical registry before restored player and policy consumers.
 * The later GAME restore verifies this same registry without importing it again. */
bool application_native_q2_checkpoint_prepare(struct application_provider *, const qa_save_record *, qa_error *);
bool application_native_q2_checkpoint_restore(struct application_provider *, qa_bytes, qa_error *);

#endif
