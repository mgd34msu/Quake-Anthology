#ifndef QA_FRONTEND_REMOTE_UNIFIED_PRESENTATION_H
#define QA_FRONTEND_REMOTE_UNIFIED_PRESENTATION_H
#include "remote_unified.h"
#include "remote_unified_media.h"
#include "unified_q3_sources.h"
#include "unified_q3_client.h"
#include "remote_unified_render.h"
#include "remote_unified_input.h"
#include "remote_unified_events.h"
#include "remote_unified_q1.h"
#include "remote_unified_q2.h"
#include "remote_unified_q3.h"
#include "remote_unified_components.h"
#include "unified_q3_runtime_factory.h"
typedef struct frontend_unified_q3_video frontend_unified_q3_video;
bool frontend_remote_unified_presentation_video_associate(frontend_remote_unified *,
    const frontend_unified_q3_video *,qa_error *);
bool frontend_remote_unified_presentation_video_current(const frontend_remote_unified *,
    const frontend_unified_q3_video *,qa_error *);
bool frontend_remote_unified_presentation_video_release(frontend_remote_unified *,
    const frontend_unified_q3_video *,qa_error *);
bool frontend_remote_unified_presentation_video_returned(const qa_frontend *,
    const frontend_video_guests *,qa_error *);
bool frontend_remote_unified_presentation_video_held(const frontend_remote_unified *);

typedef struct frontend_unified_presentation_children {
    frontend_unified_media *media,*pending_media;
    frontend_unified_render *render,*pending_render;
    frontend_remote_unified_prediction *prediction;
    frontend_unified_input *input;
    frontend_unified_events *events;
    frontend_unified_q1 *q1;
    frontend_unified_q2 *q2;
    frontend_unified_q3 *q3;
    frontend_unified_q3_sources *q3_sources;
    frontend_unified_components *components;
    uint64_t audio_owner;
} frontend_unified_presentation_children;

/* The CLIENT tuple comes from the actual remote admission owner. This factory
 * supplies the retained readonly world, frame, prediction and event children. */
bool frontend_remote_unified_presentation_create(qa_frontend *,
    const frontend_remote_unified_options *, frontend_remote_unified **, qa_error *);
bool frontend_remote_unified_presentation_restore_prefix(qa_frontend *,
    const frontend_remote_unified_options *, const qa_net_client *,
    qa_application_content_graph *, qa_bytes, frontend_remote_unified **, qa_error *);
bool frontend_remote_unified_presentation_time(const frontend_remote_unified *, double *, qa_error *);
bool frontend_remote_unified_presentation_media(const frontend_remote_unified *,
    frontend_unified_media **installed, frontend_unified_media **pending, qa_error *);
frontend_unified_q3_sources *frontend_remote_unified_presentation_q3_sources(const frontend_remote_unified *);
size_t frontend_remote_unified_presentation_q3_client_count(const frontend_remote_unified *);
frontend_unified_q3_client *frontend_remote_unified_presentation_q3_client(
    const frontend_remote_unified *,size_t);
frontend_unified_q3_runtime_factory *frontend_remote_unified_presentation_q3_factory(
    const frontend_remote_unified *,size_t);
typedef struct frontend_unified_presentation_q3_row {
    frontend_unified_q3_client *client;
    frontend_unified_q3_runtime_factory *factory;
    frontend_unified_media *media;
    frontend_unified_q3_source_view source;
    size_t bank;
    uint64_t receiver,audio_owner;
    bool retired;
} frontend_unified_presentation_q3_row;
bool frontend_remote_unified_presentation_q3_row_read(const frontend_remote_unified *,size_t,
    frontend_unified_presentation_q3_row *,qa_error *);
bool frontend_remote_unified_presentation_source_command_current(const frontend_remote_unified *,
    const char *instance,uint64_t publication,uint64_t map_revision,const qa_command_context *,qa_error *);
/* Transfer the actual detached media owners after the replica recipe prefix
 * imports. Shared dictionaries and family continuations still finish later. */
bool frontend_remote_unified_presentation_restore_media(frontend_remote_unified *,
    frontend_unified_media **installed,frontend_unified_media **pending,qa_error *);
bool frontend_remote_unified_presentation_children_read(const frontend_remote_unified *,
    frontend_unified_presentation_children *,qa_error *);
size_t frontend_remote_unified_presentation_audio_count(const frontend_remote_unified *);
bool frontend_remote_unified_presentation_audio_read(const frontend_remote_unified *,size_t,
    qa_actor_id *,uint64_t *);
bool frontend_remote_unified_presentation_trace(const frontend_remote_unified *,
    const qa_trace_query *,qa_trace_result *,qa_error *);
bool frontend_remote_unified_presentation_body(const frontend_remote_unified *,
    qa_actor_id,qa_body_state *,qa_error *);
bool frontend_remote_unified_presentation_point_contents(const frontend_remote_unified *,
    const qa_point_query *,qa_point_contents *,qa_error *);
#endif
