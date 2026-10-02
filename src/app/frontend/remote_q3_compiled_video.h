#ifndef QA_FRONTEND_REMOTE_Q3_COMPILED_VIDEO_H
#define QA_FRONTEND_REMOTE_Q3_COMPILED_VIDEO_H
#include "remote_q3_client.h"
typedef struct frontend_remote_q3_compiled_video frontend_remote_q3_compiled_video;
bool frontend_remote_q3_compiled_video_prepare(frontend_remote_q3 *,frontend_remote_q3_compiled_video **,qa_error *);
bool frontend_remote_q3_compiled_video_parent_is(const frontend_remote_q3 *,const frontend_remote_q3_compiled_video *);
bool frontend_remote_q3_compiled_video_current(const frontend_remote_q3_compiled_video *,qa_error *);
bool frontend_remote_q3_compiled_video_media_ready(const frontend_remote_q3 *,qa_error *);
bool frontend_remote_q3_compiled_video_close(frontend_remote_q3_compiled_video *,qa_error *);
bool frontend_remote_q3_compiled_video_reopen(frontend_remote_q3_compiled_video *,qa_error *);
bool frontend_remote_q3_compiled_video_finish(frontend_remote_q3_compiled_video **,qa_error *);
bool frontend_remote_q3_compiled_video_abort(frontend_remote_q3_compiled_video **,qa_error *);
#endif
