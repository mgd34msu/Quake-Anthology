#ifndef QA_APPLICATION_UNIFIED_Q3_SOURCES_H
#define QA_APPLICATION_UNIFIED_Q3_SOURCES_H

#include "unified_output.h"

typedef struct application_unified_q3_sources application_unified_q3_sources;

/* Complete copied compiled GAME observations, distinct from original component
 * ModIdentity/CL_GetServerCommand publications. No BG conversion or event ACK. */
bool application_unified_q3_sources_build(qa_application *, const application_unified_source *,
    qa_net_client_id, const qa_unified_session_player *,
    application_unified_q3_sources **empty, qa_error *);
bool application_unified_q3_sources_current(const application_unified_q3_sources *);
const qa_unified_document *application_unified_q3_sources_value(const application_unified_q3_sources *);
void application_unified_q3_sources_dispose(application_unified_q3_sources *);

#endif
