#ifndef QA_APPLICATION_NATIVE_Q3_CLIENT_MODULES_VIDEO_H
#define QA_APPLICATION_NATIVE_Q3_CLIENT_MODULES_VIDEO_H

#include "qa/application_native_q3_client_modules.h"

typedef struct qa_application_native_q3_client_modules_video qa_application_native_q3_client_modules_video;

/* Shutdown and executor destruction retain the actual acquired artifacts,
 * descriptor, CLIENT heap and GAME session. Failure leaves the ticket owned. */
bool qa_application_native_q3_client_modules_video_prepare(application_native_q3_client_modules *,
    qa_application_native_q3_client_modules_video **, qa_error *);
bool qa_application_native_q3_client_modules_video_current(const qa_application_native_q3_client_modules_video *,
    qa_error *);
/* Retry checked partial cleanup before the caller replaces any media parent. */
bool qa_application_native_q3_client_modules_video_close(qa_application_native_q3_client_modules_video *,
    qa_error *);
bool qa_application_native_q3_client_modules_video_media_ready(
    const qa_application_native_q3_client_modules_video *, qa_error *);
/* Decoded Init uses the current real connection counters, not its initial
 * construction tuple. An Initial UI supplies NULL and its connecting state.
 * Re-entry after failure disposes partial executors before reconstruction. */
bool qa_application_native_q3_client_modules_video_reopen(qa_application_native_q3_client_modules_video *,
    const qa_application_q3_remote_init *, bool connecting, qa_error *);
bool qa_application_native_q3_client_modules_video_finish(qa_application_native_q3_client_modules_video **,
    qa_error *);
/* Checked rollback reconstructs on the caller's restored physical renderer.
 * Its real media banks and color authority must already be installed. */
bool qa_application_native_q3_client_modules_video_abort(qa_application_native_q3_client_modules_video **,
    const qa_application_q3_remote_init *, bool connecting, qa_error *);

#endif
