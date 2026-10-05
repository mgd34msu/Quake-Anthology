#ifndef QA_FRONTEND_REMOTE_UNIFIED_METADATA_H
#define QA_FRONTEND_REMOTE_UNIFIED_METADATA_H

#include "remote_unified.h"

#include "qa/unified_frame_metadata.h"

bool frontend_remote_unified_metadata_control(frontend_remote_unified *,
    const qa_unified_document *, qa_error *);
const qa_unified_frame_metadata *frontend_remote_unified_metadata(const frontend_remote_unified *);
void frontend_remote_unified_metadata_clear(frontend_remote_unified *);

#endif
