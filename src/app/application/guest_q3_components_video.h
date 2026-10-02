#ifndef QA_APPLICATION_GUEST_Q3_COMPONENTS_VIDEO_H
#define QA_APPLICATION_GUEST_Q3_COMPONENTS_VIDEO_H
#include "qa/application.h"
typedef struct application_q3_components_video application_q3_components_video;
bool application_q3_components_video_prepare(qa_application *,application_q3_components_video **,qa_error *);
bool application_q3_components_video_current(const application_q3_components_video *,qa_error *);
bool application_q3_components_video_reopen(application_q3_components_video *,qa_error *);
bool application_q3_components_video_finish(application_q3_components_video **,qa_error *);
bool application_q3_components_video_abort(application_q3_components_video **,qa_error *);
#endif
