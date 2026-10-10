#ifndef QA_APPLICATION_UNIFIED_OUTPUT_H
#define QA_APPLICATION_UNIFIED_OUTPUT_H

#include "network_unified.h"
#include "qa/network_unified_control.h"
#include "qa/network_unified_frame.h"
#include "qa/unified_frame_metadata.h"

typedef struct application_unified_q3_sources application_unified_q3_sources;

typedef struct application_unified_metadata_receipt {
    uint32_t epoch;
    qa_actor_owner style_source;
    uint64_t publication_revision, roster_revision, style_revision, map_revision, q1_revision;
} application_unified_metadata_receipt;

bool application_unified_output_metadata(qa_application *, const application_unified_source *,
    const application_unified_q3_sources *, qa_unified_frame *, uint32_t epoch, const application_unified_metadata_receipt *committed,
    const qa_unified_document *committed_source_metadata,
    application_unified_metadata_receipt *proposed, qa_unified_document **, qa_error *);

/* Owned wire values observed from one returned Source frame. Resource controls
 * precede every frame that refers to them. No save image or render scene is
 * captured by this producer. */
bool application_unified_output_world(qa_application *, const application_unified_source *,
    qa_unified_frame_pool *, qa_unified_world_frame **, qa_error *);

bool application_unified_output_inventory(qa_application *, qa_actor_id, qa_unified_frame *,
    const qa_inventory_entry **raw, size_t *, qa_error *);

/* Children borrow genuine Source projections produced for this same receiver.
 * Values are immutable during assembly; current proves their actual owners
 * before and after observation, independently of selected CHARACTER. */
typedef struct application_unified_frame_children {
    void *context;
    bool (*current)(void *, qa_application *, const application_unified_source *,
        qa_net_client_id, const qa_unified_session_player *);
    /* Actual resource/event/component reliable updates, in producer order. */
    const qa_unified_document *const *controls;
    size_t control_count;
} application_unified_frame_children;
typedef struct application_unified_output {
    qa_unified_document *frame;
    qa_unified_document **controls;
    size_t control_count;
    bool controls_pooled;
} application_unified_output;

bool application_unified_output_build(qa_application *, const application_unified_source *,
    qa_net_client_id, const qa_unified_session_player *, qa_unified_frame **,
    const application_unified_frame_children *,
    application_unified_output *, qa_error *);
void application_unified_output_dispose(application_unified_output *);

/* The caller supplies a genuine retained resource acquisition and its owning
 * product/requested path. The dictionary assigns the serial before publication. */
bool application_unified_resource_key(uint64_t serial, const qa_product *, const char *, const qa_resource *,
    qa_unified_document **key, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], qa_error *);
bool application_unified_resource_control(uint32_t epoch, qa_unified_frame_lease *,
    const qa_unified_resource_declaration *, size_t count, qa_unified_document **, qa_error *);

#endif
