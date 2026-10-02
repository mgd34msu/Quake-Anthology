#ifndef QA_APPLICATION_UNIFIED_COMPONENTS_H
#define QA_APPLICATION_UNIFIED_COMPONENTS_H

#include "network_unified.h"

typedef struct application_unified_component_publisher application_unified_component_publisher;
typedef struct application_unified_component_capture application_unified_component_capture;

/* One admitted remote recipient owns the reliable roster/config/command
 * cursor. A prepared publication retains its actual GAME source leases until
 * it is disposed; failed queueing never advances the publisher. */
bool application_unified_components_create(qa_application *, qa_net_client_id,
    qa_actor_id, application_unified_component_publisher **, qa_error *);
bool application_unified_components_idle(const application_unified_component_publisher *);
bool application_unified_components_destroy(application_unified_component_publisher **, qa_error *);
bool application_unified_components_prepare(application_unified_component_publisher *,
    const application_unified_source *, const qa_unified_session_player *, uint32_t epoch,
    const qa_unified_document *player_values, application_unified_component_capture **, qa_error *);
bool application_unified_components_current(const application_unified_component_capture *);
const qa_unified_document *application_unified_components_frame(const application_unified_component_capture *);
const qa_unified_document *application_unified_components_control(const application_unified_component_capture *);
bool application_unified_components_seal(application_unified_component_capture *, qa_error *);
/* Called once after the already-owned reliable controls and FRAME have been
 * queued. It performs no source call, allocation, clock read or replay. */
void application_unified_components_commit(application_unified_component_capture *);
void application_unified_components_dispose(application_unified_component_capture *);

#endif
