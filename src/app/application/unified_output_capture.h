#ifndef QA_APPLICATION_UNIFIED_OUTPUT_CAPTURE_H
#define QA_APPLICATION_UNIFIED_OUTPUT_CAPTURE_H

#include "unified_output.h"
#include "unified_player.h"
#include "unified_components.h"

typedef struct application_unified_output_capture application_unified_output_capture;
/* The component publication owner retains these actual immutable documents
 * and its receipt until capture is disposed. QC additionally supplies its
 * real decoded camera/STAT3 receipt; direct Source producers need no override. */
typedef struct application_unified_output_external {
    void *context;
    bool (*current)(void *, qa_application *, const application_unified_source *,
        qa_net_client_id, const qa_unified_session_player *);
    const application_unified_player_external *player;
    const qa_unified_document *components, *native_camera;
    const qa_unified_document *const *controls;
    size_t control_count;
} application_unified_output_external;

bool application_unified_output_acquire(qa_application *, const application_unified_source *,
    qa_net_client_id, const qa_unified_session_player *, uint32_t epoch,
    int64_t acknowledged_input, uint64_t events_after,
    application_unified_component_publisher *, const application_unified_output_external *,
    application_unified_output_capture **, qa_error *);
const application_unified_output *application_unified_output_capture_value(const application_unified_output_capture *);
uint64_t application_unified_output_capture_events_through(const application_unified_output_capture *);
bool application_unified_output_capture_current(const application_unified_output_capture *);
bool application_unified_output_capture_seal(application_unified_output_capture *, qa_error *);
void application_unified_output_capture_commit(application_unified_output_capture *);
void application_unified_output_capture_dispose(application_unified_output_capture *);

#endif
