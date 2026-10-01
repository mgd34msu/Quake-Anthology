#ifndef QA_LAUNCH_NATIVE_SAVE_H
#define QA_LAUNCH_NATIVE_SAVE_H

#include "qa/launch_save.h"

/* The installed native GAME already owns this restored implementation. Verify
 * the complete retained recipe, then retain that actual immutable storage with
 * its saved snapshot-local roles. The recipe and content remain borrowed;
 * no view is claimed, cloned or reopened by this operation. */
bool qa_launch_instance_restore_native_metadata(const qa_launch_instance *,
    const qa_launch_restored_instance *, uint64_t roles,
    qa_launch_instance_lease **, qa_error *);

#endif
