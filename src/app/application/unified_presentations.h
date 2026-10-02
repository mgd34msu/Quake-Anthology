#ifndef QA_APPLICATION_UNIFIED_PRESENTATIONS_H
#define QA_APPLICATION_UNIFIED_PRESENTATIONS_H

#include "unified_output.h"

typedef struct application_unified_presentations {
    application_unified_source source;
    uint64_t actors_revision;
    qa_unified_document *models, *characters;
} application_unified_presentations;

/* Observe genuine canonical visual providers and physical native Q3 rows.
 * Original component GAME appearances are supplied by their component owner;
 * these arrays neither execute CGAME nor construct a selected model path. */
bool application_unified_presentations_build(qa_application *,
    const application_unified_source *, qa_net_client_id,
    const qa_unified_session_player *, application_unified_presentations *, qa_error *);
bool application_unified_presentations_current(qa_application *,
    const application_unified_presentations *);
void application_unified_presentations_dispose(application_unified_presentations *);

#endif
