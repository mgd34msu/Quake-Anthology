#ifndef QA_FRONTEND_UNIFIED_Q3_RUNTIME_VIDEO_H
#define QA_FRONTEND_UNIFIED_Q3_RUNTIME_VIDEO_H
#include "unified_q3_runtime.h"
typedef struct frontend_unified_q3_runtime_video frontend_unified_q3_runtime_video;
bool frontend_unified_q3_runtime_video_prepare(frontend_unified_q3_runtime *,
    frontend_unified_q3_runtime_video **,qa_error *);
bool frontend_unified_q3_runtime_video_current(const frontend_unified_q3_runtime_video *,qa_error *);
bool frontend_unified_q3_runtime_video_returned(const frontend_unified_q3_runtime *,
    const frontend_video_guests *,qa_error *);
const frontend_unified_q3_client_video *frontend_unified_q3_runtime_video_client(
    const frontend_unified_q3_runtime_video *);
bool frontend_unified_q3_runtime_video_close(frontend_unified_q3_runtime_video *,qa_error *);
bool frontend_unified_q3_runtime_video_reopen(frontend_unified_q3_runtime_video *,qa_error *);
bool frontend_unified_q3_runtime_video_finish(frontend_unified_q3_runtime_video **,qa_error *);
bool frontend_unified_q3_runtime_video_abort(frontend_unified_q3_runtime_video **,qa_error *);
#endif
