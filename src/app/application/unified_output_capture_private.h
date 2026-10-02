#ifndef QA_APPLICATION_UNIFIED_OUTPUT_CAPTURE_PRIVATE_H
#define QA_APPLICATION_UNIFIED_OUTPUT_CAPTURE_PRIVATE_H

#include "unified_output_capture.h"
#include "unified_presentations.h"
#include "unified_events.h"
#include "unified_q3_sources.h"

struct application_unified_output_capture {
    qa_application *application;
    application_unified_source source;
    qa_net_client_id recipient;
    qa_unified_session_player player;
    application_unified_output_external external;
    bool has_external;
    uint64_t actors_revision;
    application_unified_presentations visuals;
    application_unified_events events;
    application_unified_component_capture *components;
    application_unified_q3_sources *q3_sources;
    bool sealed;
    qa_unified_document *prediction, *player_values, *presentation, *frame_events;
    application_unified_output output;
};

#endif
