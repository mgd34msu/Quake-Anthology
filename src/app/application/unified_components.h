#ifndef QA_APPLICATION_UNIFIED_COMPONENTS_H
#define QA_APPLICATION_UNIFIED_COMPONENTS_H

#include "network_unified.h"
#include "qa/unified_frame_components.h"
#include "qa/unified_frame_player.h"

typedef struct application_unified_component_publisher application_unified_component_publisher;
typedef struct application_unified_component_capture application_unified_component_capture;

/* One admitted remote recipient owns the reliable roster/config/command
 * cursor. Preparation borrows actual GAME publications; seal returns those
 * leases and retains the immutable queue token. FRAME rows and capture scratch
 * share the target lease; dispose releases its retained reference last.
 * Failed queueing never advances the publisher. */
bool application_unified_components_create(qa_application *, qa_net_client_id,
    qa_actor_id, application_unified_component_publisher **, qa_error *);
bool application_unified_components_idle(const application_unified_component_publisher *);
bool application_unified_components_destroy(application_unified_component_publisher **, qa_error *);
bool application_unified_components_prepare(application_unified_component_publisher *,
    const application_unified_source *, const qa_unified_session_player *, uint32_t epoch,
    qa_unified_frame *target, const qa_unified_frame_player *player_values,
    application_unified_component_capture **, qa_error *);
bool application_unified_components_current(const application_unified_component_capture *);
const qa_unified_frame_components *application_unified_components_frame(const application_unified_component_capture *);
const qa_unified_document *application_unified_components_control(const application_unified_component_capture *);
qa_unified_frame_components *application_unified_components_take(application_unified_component_capture *);
bool application_unified_components_bind_frame(application_unified_component_capture *,
    const qa_unified_document *, qa_error *);
bool application_unified_components_seal(application_unified_component_capture *,
    const qa_unified_document *frame, qa_error *);
/* Called once after the already-owned reliable controls and FRAME have been
 * queued. It performs no source call, allocation, clock read or replay. */
void application_unified_components_commit(application_unified_component_capture *);
void application_unified_components_dispose(application_unified_component_capture *);

#endif
