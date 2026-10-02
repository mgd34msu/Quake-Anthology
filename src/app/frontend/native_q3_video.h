#ifndef QA_FRONTEND_NATIVE_Q3_VIDEO_H
#define QA_FRONTEND_NATIVE_Q3_VIDEO_H
#include "native_q3_client.h"
typedef struct frontend_native_q3_video frontend_native_q3_video;
bool frontend_native_q3_video_prepare(qa_frontend *,frontend_native_q3_video **,qa_error *);
bool frontend_native_q3_video_current(const frontend_native_q3_video *,qa_error *);
bool frontend_native_q3_video_read(const qa_frontend *,size_t,frontend_native_q3_view *,qa_error *);
bool frontend_native_q3_video_reopen(frontend_native_q3_video *,qa_error *);
bool frontend_native_q3_video_finish(frontend_native_q3_video **,qa_error *);
bool frontend_native_q3_video_abort(frontend_native_q3_video **,qa_error *);
#endif
