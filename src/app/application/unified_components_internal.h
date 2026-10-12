#ifndef QA_APPLICATION_UNIFIED_COMPONENTS_INTERNAL_H
#define QA_APPLICATION_UNIFIED_COMPONENTS_INTERNAL_H

#include "unified_components.h"
#include "guest_q3_components.h"
#include "unified_q2_components.h"

enum { APPLICATION_UNIFIED_COMPONENT_CURSORS = 256 };

typedef struct component_cursor {
    qa_actor_owner owner;
    uint64_t generation;
    int64_t game_state_revision;
    int32_t sequence;
    qa_qvm_abi abi;
    bool scene;
} component_cursor;
typedef struct native_cursor {
    qa_actor_owner owner;
    uint64_t activation, generation;
    uint64_t configuration_revision, layout_revision, inventory_revision;
    uint32_t source_slot;
    bool present;
} native_cursor;
struct application_unified_component_publisher {
    qa_application *application;
    qa_net_client_id recipient;
    qa_actor_id actor;
    uint64_t revision, serial;
    uint32_t epoch;
    component_cursor *rows;
    size_t count;
    qa_arena storage;
    native_cursor native;
    application_unified_component_capture *pending;
};
struct application_unified_component_capture {
    application_unified_component_publisher *owner;
    qa_unified_frame *target;
    qa_unified_frame_lease *lease;
    application_unified_source source;
    qa_unified_session_player player;
    application_q3_components *roster;
    application_q3_component_publication_lease **leases;
    component_cursor *rows;
    size_t count;
    uint64_t revision, serial;
    uint32_t epoch;
    qa_unified_frame_components *frame;
    qa_unified_document *frame_document, *control;
    application_unified_q2_component_documents native_documents;
    native_cursor native;
    bool sealed, committed;
};

bool application_unified_components_storage_create(application_unified_component_publisher *, qa_error *);

#endif
