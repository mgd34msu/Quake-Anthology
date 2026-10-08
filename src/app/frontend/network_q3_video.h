#ifndef QA_FRONTEND_NETWORK_Q3_VIDEO_H
#define QA_FRONTEND_NETWORK_Q3_VIDEO_H
#include "network_presentation.h"
#include "remote_q3_initial.h"
#include "video_guests.h"
typedef struct frontend_network_q3_video_reinit_view {
    const qa_frontend_network *network;
    const frontend_video_guests *video_stage;
    frontend_network_client_domain domain;
    qa_application_q3_remote_init init;
    bool connecting;
} frontend_network_q3_video_reinit_view;
bool frontend_network_q3_video_reinit_read(const qa_frontend *,
    const qa_application_q3_client_context *,frontend_network_q3_video_reinit_view *,qa_error *);
bool frontend_network_q3_video_reinit_current(const qa_frontend *,const frontend_network_q3_video_reinit_view *);
bool frontend_network_q3_video_initial_read(const qa_frontend *,const frontend_remote_q3_initial *,
    frontend_remote_q3_modules **,qa_error *);
#endif
