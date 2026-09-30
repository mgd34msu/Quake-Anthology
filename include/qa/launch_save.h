#ifndef QA_LAUNCH_SAVE_H
#define QA_LAUNCH_SAVE_H
#include "qa/launch.h"

typedef struct qa_configuration_checkpoint {
    uint64_t generation;
    bool has_current;
} qa_configuration_checkpoint;
bool qa_configuration_checkpoint_capture(const qa_configuration *, qa_configuration_checkpoint *, qa_error *);
/* Fresh isolated manager only. Preparation retains real content and constructs
 * actual provider states. Ordinary publication callbacks never run on this
 * path: the application must restore routing, world and source owners itself
 * before publishing the complete isolated application. */
bool qa_configuration_prepare_restored(qa_configuration *, const qa_launch_draft *,
    const qa_configuration_checkpoint *, qa_configuration_transaction **, qa_error *);
bool qa_configuration_commit_restored(qa_configuration_transaction *, qa_error *);
#endif
