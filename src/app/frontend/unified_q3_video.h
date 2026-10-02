#ifndef QA_FRONTEND_UNIFIED_Q3_VIDEO_H
#define QA_FRONTEND_UNIFIED_Q3_VIDEO_H
#include "unified_q3_runtime_video.h"
typedef struct frontend_unified_q3_video frontend_unified_q3_video;
bool frontend_unified_q3_video_prepare(qa_frontend *,frontend_remote_unified *,
    frontend_unified_q3_video **,qa_error *);
bool frontend_unified_q3_video_current(const frontend_unified_q3_video *,qa_error *);
bool frontend_unified_q3_video_returned(const frontend_unified_q3_video *,
    const frontend_video_guests *,qa_error *);
bool frontend_unified_q3_video_reopen(frontend_unified_q3_video *,qa_error *);
bool frontend_unified_q3_video_finish(frontend_unified_q3_video **,qa_error *);
bool frontend_unified_q3_video_abort(frontend_unified_q3_video **,qa_error *);
#endif
