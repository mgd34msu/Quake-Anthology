#ifndef QA_APPLICATION_UNIFIED_Q2_COMPONENTS_H
#define QA_APPLICATION_UNIFIED_Q2_COMPONENTS_H

#include "network_unified.h"
#include "qa/unified_frame_components.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/network_q2.h"
#include "native_q2_publication.h"

typedef struct application_unified_q2_source_documents {
    application_unified_source source;
    qa_application_native_q2_presentation physical;
    qa_net_client_id recipient;
    qa_unified_session_player player;
    uint64_t actors_revision, config_revision;
    qa_q2_player player_state;
    char layout[1024];
    int16_t inventory[256];
    qa_unified_document *hud_state;
    qa_unified_native_hud hud_frame;
    qa_unified_native_camera camera;
    bool present;
} application_unified_q2_source_documents;

/* Observes the actual original primary Q2 Source for this remote full actor.
 * Its reliable setup and typed frame values grant no mod identity, presentation
 * ownership or component membership. A registered component supplies those. */
bool application_unified_q2_source_documents_build(qa_application *,
    const application_unified_source *, qa_net_client_id,
    const qa_unified_session_player *, qa_unified_frame *target,
    application_unified_q2_source_documents *, qa_error *);
bool application_unified_q2_source_documents_current(qa_application *,
    const application_unified_q2_source_documents *);
void application_unified_q2_source_documents_dispose(application_unified_q2_source_documents *);

typedef struct application_unified_q2_component_documents {
    application_unified_q2_source_documents source;
    application_native_q2_publication_view publication;
    qa_unified_document *state;
    qa_unified_frame_components *frame;
    bool present;
} application_unified_q2_component_documents;

/* Full reliable/native frame rows for the real registered component. The
 * recipient publisher owns shared reliable revision and config delta policy. */
bool application_unified_q2_component_documents_build(qa_application *,
    const application_unified_source *, qa_net_client_id, const qa_unified_session_player *,
    qa_unified_frame *target, application_unified_q2_component_documents *, qa_error *);
bool application_unified_q2_component_documents_current(qa_application *,
    const application_unified_q2_component_documents *);
void application_unified_q2_component_documents_dispose(application_unified_q2_component_documents *);

#endif
