#ifndef QA_APPLICATION_UNIFIED_COMPONENTS_SAVE_H
#define QA_APPLICATION_UNIFIED_COMPONENTS_SAVE_H

#include "unified_components.h"

/* Committed recipient cursors and sealed immutable queue tokens are separate
 * children. Import follows actual Source/player and component-owner import;
 * it never enters GAME, builds a viewer snapshot or reacquires a lease. */
bool application_unified_components_checkpoint(const application_unified_component_publisher *,
    qa_buffer *, qa_error *);
bool application_unified_components_restore(qa_bytes, qa_application *,
    const application_unified_source *, qa_net_client_id, const qa_unified_session_player *,
    application_unified_component_publisher **, qa_error *);
bool application_unified_components_checkpoint_retained(const application_unified_component_publisher *,
    const application_unified_source *current, const application_unified_source *retained,
    const qa_unified_session_player *, qa_buffer *, qa_error *);
bool application_unified_components_restore_retained(qa_bytes, qa_application *,
    const application_unified_source *current, const application_unified_source *retained,
    qa_net_client_id, const qa_unified_session_player *, application_unified_component_publisher **, qa_error *);
/* A real GAME DROP retains committed recipient cursors until returned child
 * cleanup finishes. Its owned bridge receipt grants no current player. */
bool application_unified_components_checkpoint_dropped(const application_unified_component_publisher *,
    const application_unified_server *, qa_buffer *, qa_error *);
bool application_unified_components_restore_dropped(qa_bytes, const application_unified_server *,
    application_unified_component_publisher **, qa_error *);
bool application_unified_components_capture_checkpoint(const application_unified_component_capture *,
    qa_buffer *, qa_error *);
bool application_unified_components_capture_restore(qa_bytes,
    application_unified_component_publisher *, const application_unified_source *,
    const qa_unified_session_player *, const qa_unified_document *frame,
    application_unified_component_capture **, qa_error *);

#endif
