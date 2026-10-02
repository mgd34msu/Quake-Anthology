#ifndef QA_FRONTEND_VIDEO_GUESTS_H
#define QA_FRONTEND_VIDEO_GUESTS_H
#include "internal.h"
typedef struct frontend_video_guests frontend_video_guests;

/* The retained ticket owns every admitted guest reconstruction and remains
 * reachable when preparation, reconstruction or cleanup refuses. */
bool frontend_video_guests_prepare(qa_frontend *,frontend_video_guests **,qa_error *);
bool frontend_video_guests_current(const frontend_video_guests *,qa_error *);
bool frontend_video_guests_reopen(frontend_video_guests *,qa_error *);
bool frontend_video_guests_finish(frontend_video_guests **,qa_error *);
/* The controller restores its chosen physical output before abort. */
bool frontend_video_guests_abort(frontend_video_guests **,qa_error *);
/* Direct installed-parent association; no guest callbacks or clocks. */
const frontend_video_guests *frontend_video_guests_read(const qa_frontend *);
bool frontend_video_guests_parent_is(const qa_frontend *,const frontend_video_guests *);
bool frontend_video_guests_resources_associated(const qa_frontend *,const frontend_video_guests *);
bool frontend_video_guests_resources_returned(const qa_frontend *,const frontend_video_guests *,qa_error *);
#endif
