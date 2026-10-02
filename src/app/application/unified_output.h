#ifndef QA_APPLICATION_UNIFIED_OUTPUT_H
#define QA_APPLICATION_UNIFIED_OUTPUT_H

#include "network_unified.h"

/* Owned wire values observed from one returned Source frame. Resource controls
 * precede every frame that refers to them. No save image or render scene is
 * captured by this producer. */
typedef struct application_unified_snapshot {
    qa_unified_document *snapshot;
} application_unified_snapshot;

bool application_unified_output_snapshot(qa_application *, const application_unified_source *,
    qa_net_client_id, const qa_unified_session_player *, uint32_t epoch,
    application_unified_snapshot *, qa_error *);
void application_unified_snapshot_dispose(application_unified_snapshot *);

/* Children borrow genuine Source projections produced for this same receiver.
 * Values are immutable during assembly; current proves their actual owners
 * before and after observation, independently of selected CHARACTER. */
typedef struct application_unified_frame_children {
    void *context;
    bool (*current)(void *, qa_application *, const application_unified_source *,
        qa_net_client_id, const qa_unified_session_player *);
    const qa_unified_document *prediction;
    /* CHECKPOINT record: models, characters, worldText, player {view, ui},
     * optional nativeCamera and components; actual producer owns each field. */
    const qa_unified_document *presentation;
    /* CHECKPOINT array of actual recipient-filtered SimulationEvents. */
    const qa_unified_document *simulation_events;
    /* Actual resource/event/component reliable updates, in producer order. */
    const qa_unified_document *const *controls;
    size_t control_count;
} application_unified_frame_children;
typedef struct application_unified_output {
    qa_unified_document *frame;
    qa_unified_document **controls;
    size_t control_count;
} application_unified_output;

bool application_unified_output_build(qa_application *, const application_unified_source *,
    qa_net_client_id, const qa_unified_session_player *, uint32_t epoch,
    int64_t acknowledged_input, const application_unified_frame_children *,
    application_unified_output *, qa_error *);
void application_unified_output_dispose(application_unified_output *);

/* The caller supplies a genuine retained resource acquisition and its owning
 * product/requested path. IDs use the donor's exact ResourceKey tuple. */
bool application_unified_resource_key(const qa_product *, const char *, const qa_resource *,
    qa_unified_document **key, char id[QA_APPLICATION_RESOURCE_KEY_CAPACITY], qa_error *);
bool application_unified_resource_control(uint32_t epoch,
    const qa_unified_document *const *keys, size_t count, qa_unified_document **, qa_error *);

#endif
