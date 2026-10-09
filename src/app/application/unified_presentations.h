#ifndef QA_APPLICATION_UNIFIED_PRESENTATIONS_H
#define QA_APPLICATION_UNIFIED_PRESENTATIONS_H

#include "unified_output.h"
#include "qa/unified_frame_visuals.h"
#include "qa/application_visual_visibility.h"

typedef struct application_unified_presentations {
    application_unified_source source;
    uint64_t actors_revision;
    const qa_unified_frame_visuals *value;
} application_unified_presentations;

/* Observe genuine canonical visual providers and physical native Q3 rows.
 * Original component GAME appearances are supplied by their component owner;
 * these arrays neither execute CGAME nor construct a selected model path. */
bool application_unified_presentations_build(qa_application *,
    const application_unified_source *, qa_net_client_id,
    const qa_unified_session_player *, qa_application_visual_visibility *,
    qa_unified_frame *, application_unified_presentations *, qa_error *);
bool application_unified_presentations_current(qa_application *,
    const application_unified_presentations *);

#endif
