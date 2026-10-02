#ifndef QA_FRONTEND_REMOTE_Q3_MODULES_VIDEO_H
#define QA_FRONTEND_REMOTE_Q3_MODULES_VIDEO_H

#include "remote_q3_modules.h"

typedef struct frontend_remote_q3_modules_video frontend_remote_q3_modules_video;

/* Actual Shutdown and checked frontend lease disposal precede renderer/color
 * retirement. Every failed stage stays owned in the returned ticket. */
bool frontend_remote_q3_modules_video_prepare(frontend_remote_q3_modules *,
    frontend_remote_q3_modules_video **, qa_error *);
bool frontend_remote_q3_modules_video_current(const frontend_remote_q3_modules_video *, qa_error *);
bool frontend_remote_q3_modules_video_close(frontend_remote_q3_modules_video *, qa_error *);
/* Exact closed-ticket proof for its real parent media replacement producer;
 * this does not make ordinary module idle or capture readiness true. */
bool frontend_remote_q3_modules_video_media_ready(const frontend_remote_q3_modules *, qa_error *);
/* The parent must first admit a successful newer media generation. Closing a
 * partial reconstruction requires another genuine media rebuild before retry.
 * Reopen reconstructs acquired hosts against those actual current parent roots.
 * It does not refresh images or replay a saved initial connection tuple. */
bool frontend_remote_q3_modules_video_reopen(frontend_remote_q3_modules_video *,
    const qa_application_q3_remote_init *, bool connecting, qa_error *);
bool frontend_remote_q3_modules_video_finish(frontend_remote_q3_modules_video **, qa_error *);
bool frontend_remote_q3_modules_video_abort(frontend_remote_q3_modules_video **,
    const qa_application_q3_remote_init *, bool connecting, qa_error *);

#endif
