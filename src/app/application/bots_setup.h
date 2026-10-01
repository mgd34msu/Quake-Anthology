#ifndef QA_APPLICATION_BOTS_SETUP_H
#define QA_APPLICATION_BOTS_SETUP_H
#include "bots_private.h"

bool application_bots_prepare_requested(qa_application *,const qa_launch_choices *,
    const qa_bsp_view *,qa_error *);
bool application_bots_source_initialize(application_bots *,qa_error *);
bool application_bots_requested(qa_application *,qa_error *);
bool application_bots_frame_request(qa_application *,const qa_source_frame *,size_t,uint64_t,qa_error *);
#endif
