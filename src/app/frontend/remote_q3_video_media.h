#ifndef QA_FRONTEND_REMOTE_Q3_VIDEO_MEDIA_H
#define QA_FRONTEND_REMOTE_Q3_VIDEO_MEDIA_H
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
/* Requires the exact closed acquired-module video ticket. The physical CLIENT
 * graph remains installed while its renderer registration parents are rebuilt. */
bool frontend_remote_q3_resources_video_refresh(frontend_remote_q3 *,
    frontend_remote_q3_modules *,uint64_t *,qa_error *);
bool frontend_remote_q3_initial_video_refresh(frontend_remote_q3_initial *,
    frontend_remote_q3_modules *,uint64_t *,qa_error *);
/* Structural physical CLIENT proof during a retained video attempt, including
 * a failed partial bank reconstruction. Complete is literal media readiness. */
bool frontend_remote_q3_resources_video_read(const frontend_remote_q3 *,
    const frontend_remote_q3_modules *,frontend_remote_q3_resources *,uint64_t *,bool *,qa_error *);
bool frontend_remote_q3_initial_video_read(const frontend_remote_q3_initial *,
    const frontend_remote_q3_modules *,frontend_remote_q3_initial_view *,uint64_t *,bool *,qa_error *);
#endif
