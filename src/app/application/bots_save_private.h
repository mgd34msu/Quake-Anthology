#ifndef QA_APPLICATION_BOTS_SAVE_PRIVATE_H
#define QA_APPLICATION_BOTS_SAVE_PRIVATE_H

#include "bots_private.h"
#include "qa/persistence_content.h"
#include "qa/source_save.h"

bool application_bots_content_visit(const qa_application *,const qa_application_content_visitor *,qa_error *);
bool application_bot_resource_field(qa_source_save_io *,qa_application *,qa_vfs *,
    qa_resource **,qa_vfs_acquisition *,bool prepare);

/* Prepare under the candidate persistence lease after pinned map, shared
 * world/physics/modes and provider routing exist, before guest bot binding.
 * The application owns all partial candidates for retryable teardown. */
bool application_bots_save_capture(qa_application *,qa_buffer *,qa_error *);
bool application_navigation_save_capture(qa_application *,qa_buffer *,qa_error *);
bool application_bots_save_prepare(qa_application *,qa_bytes bots,qa_bytes navigation,qa_error *);
/* Shared actors/controls/roster/modes must be restored first. Navigation import
 * precedes full runtime and population import, with stable service addresses. */
bool application_navigation_save_restore(qa_application *,qa_bytes,qa_error *);
bool application_bots_save_restore(qa_application *,qa_bytes,qa_error *);
bool application_bots_save_finish(qa_application *,qa_error *);

#endif
