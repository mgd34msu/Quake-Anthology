#ifndef QA_FRONTEND_REMOTE_UNIFIED_METADATA_H
#define QA_FRONTEND_REMOTE_UNIFIED_METADATA_H

#include "remote_unified.h"

#include "qa/unified_frame_metadata.h"

bool frontend_remote_unified_metadata_control(frontend_remote_unified *,
    const qa_unified_document *, qa_error *);
/* Published consumers keep their applied cut; preparation borrows the cut
 * selected for that exact retained FRAME. */
const qa_unified_frame_metadata *frontend_remote_unified_metadata(const frontend_remote_unified *);
const qa_unified_document *frontend_remote_unified_metadata_document(const frontend_remote_unified *,
    const qa_unified_frame *);
bool frontend_remote_unified_metadata_prepare(frontend_remote_unified *, const qa_unified_frame *, qa_error *);
void frontend_remote_unified_metadata_commit(frontend_remote_unified *, const qa_unified_frame *);
void frontend_remote_unified_metadata_abort(frontend_remote_unified *);
bool frontend_remote_unified_metadata_returned(const frontend_remote_unified *, qa_error *);
void frontend_remote_unified_metadata_clear(frontend_remote_unified *);

#endif
