#ifndef QA_APPLICATION_UNIFIED_Q3_SOURCES_H
#define QA_APPLICATION_UNIFIED_Q3_SOURCES_H

#include "unified_output.h"
#include "qa/unified_frame_q3.h"

typedef struct application_unified_q3_sources application_unified_q3_sources;

/* GAME observations and capture scratch use the actual target FRAME lease.
 * The scratch retains that lease until dispose, including on capture failure. */
bool application_unified_q3_sources_build(qa_application *, const application_unified_source *,
    qa_net_client_id, const qa_unified_session_player *,
    qa_unified_frame *target, application_unified_q3_sources **empty, qa_error *);
bool application_unified_q3_sources_current(const application_unified_q3_sources *);
bool application_unified_q3_sources_metadata_current(const application_unified_q3_sources *,
    const qa_unified_document *committed);
bool application_unified_q3_sources_metadata(const application_unified_q3_sources *,
    qa_unified_frame_metadata *empty, qa_error *);
const qa_unified_frame_q3 *application_unified_q3_sources_value(const application_unified_q3_sources *);
qa_unified_frame_q3 *application_unified_q3_sources_take(application_unified_q3_sources *);
void application_unified_q3_sources_dispose(application_unified_q3_sources *);

#endif
