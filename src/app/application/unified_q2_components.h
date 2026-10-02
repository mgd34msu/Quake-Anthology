#ifndef QA_APPLICATION_UNIFIED_Q2_COMPONENTS_H
#define QA_APPLICATION_UNIFIED_Q2_COMPONENTS_H

#include "network_unified.h"
#include "qa/application_native_q2_presentation.h"
#include "qa/network_q2.h"

typedef struct application_unified_q2_source_documents {
    application_unified_source source;
    qa_application_native_q2_presentation physical;
    qa_net_client_id recipient;
    qa_unified_session_player player;
    uint64_t actors_revision, config_revision;
    qa_q2_player player_state;
    char layout[1024];
    int16_t inventory[256];
    qa_unified_document *hud_state, *hud_frame, *camera;
    bool present;
} application_unified_q2_source_documents;

/* Observes the actual original primary Q2 Source for this remote full actor.
 * These raw CHECKPOINT children grant no mod identity, presentation ownership
 * or component membership. A registered component owner supplies those. */
bool application_unified_q2_source_documents_build(qa_application *,
    const application_unified_source *, qa_net_client_id,
    const qa_unified_session_player *, application_unified_q2_source_documents *, qa_error *);
bool application_unified_q2_source_documents_current(qa_application *,
    const application_unified_q2_source_documents *);
void application_unified_q2_source_documents_dispose(application_unified_q2_source_documents *);

#endif
