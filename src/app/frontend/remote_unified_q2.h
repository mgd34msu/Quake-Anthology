#ifndef QA_FRONTEND_REMOTE_UNIFIED_Q2_H
#define QA_FRONTEND_REMOTE_UNIFIED_Q2_H
#include "remote_unified_events.h"
#include "remote_q2_effects.h"
#include "qa/persistence_content.h"

typedef struct frontend_unified_q2 frontend_unified_q2;
typedef struct frontend_unified_q2_refs {
    qa_application_content_graph *content;
    frontend_remote_q2_effects_refs effects;
    bool (*model_encode)(void *, const qa_scene_model *, uint64_t *, qa_error *);
    bool (*model_decode)(void *, uint64_t, qa_scene_model **, qa_error *);
} frontend_unified_q2_refs;
bool frontend_unified_q2_create(qa_frontend *, frontend_remote_unified *, frontend_unified_media *,
    frontend_unified_events *, frontend_unified_q2 **, qa_error *);
bool frontend_unified_q2_validate(frontend_unified_q2 *, bool, const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q2_owner_validate(frontend_unified_q2 *, const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q2_owner_retire(frontend_unified_q2 *, const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q2_presentation(frontend_unified_q2 *, const qa_unified_document *, qa_json_id, bool *, qa_error *);
bool frontend_unified_q2_simulation(frontend_unified_q2 *, const qa_unified_document *, qa_json_id, qa_error *);
bool frontend_unified_q2_frame_prepare(frontend_unified_q2 *, const qa_unified_document *, qa_error *);
bool frontend_unified_q2_frame_ready(frontend_unified_q2 *, const qa_unified_document *, qa_error *);
void frontend_unified_q2_frame_commit(frontend_unified_q2 *);
void frontend_unified_q2_frame_abort(frontend_unified_q2 *);
bool frontend_unified_q2_world(frontend_unified_q2 *, const qa_scene_view *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_world_input(frontend_unified_q2 *, qa_scene_world_input *, qa_error *);
bool frontend_unified_q2_lights(frontend_unified_q2 *, const qa_scene_view *, const qa_scene_world_input *, const qa_scene_light **, size_t *, qa_error *);
bool frontend_unified_q2_hud(frontend_unified_q2 *, qa_ui *, qa_scene_rect, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_model(frontend_unified_q2 *, qa_actor_id, const char *, const char *, qa_scene_model_input *, qa_error *);
bool frontend_unified_q2_model_after(frontend_unified_q2 *, qa_actor_id, const char *, const char *, const qa_scene_model_input *, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_idle(const frontend_unified_q2 *);
bool frontend_unified_q2_destroy(frontend_unified_q2 **, qa_error *);
bool frontend_unified_q2_visit(const frontend_unified_q2 *, const qa_application_content_visitor *, qa_error *);
bool frontend_unified_q2_checkpoint(frontend_unified_q2 *, const frontend_unified_q2_refs *, qa_buffer *, qa_error *);
bool frontend_unified_q2_restore(qa_frontend *, frontend_remote_unified *, frontend_unified_media *,
    frontend_unified_events *, const frontend_unified_q2_refs *, qa_bytes, frontend_unified_q2 **, qa_error *);
#endif
