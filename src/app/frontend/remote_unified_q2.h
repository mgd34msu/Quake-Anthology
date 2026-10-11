#ifndef QA_FRONTEND_REMOTE_UNIFIED_Q2_H
#define QA_FRONTEND_REMOTE_UNIFIED_Q2_H
#include "remote_unified_events.h"
#include "remote_q2_effects.h"
#include "qa/persistence_content.h"
#include "qa/ui.h"
#include "qa/unified_frame_events.h"

typedef struct frontend_unified_q2 frontend_unified_q2;
bool frontend_unified_q2_create(qa_frontend *, frontend_remote_unified *, frontend_unified_media *,
    frontend_unified_events *, frontend_unified_q2 **, qa_error *);
bool frontend_unified_q2_components_control(frontend_unified_q2 *,const qa_unified_document *,qa_error *);
bool frontend_unified_q2_status_replacement(const frontend_unified_q2 *,bool *,qa_error *);
bool frontend_unified_q2_presentation_validate(frontend_unified_q2 *, const qa_unified_presentation_event *, qa_error *);
bool frontend_unified_q2_simulation_validate(frontend_unified_q2 *, const qa_unified_simulation_event *, qa_error *);
bool frontend_unified_q2_owner_validate(frontend_unified_q2 *, const qa_unified_presentation_event *, qa_error *);
bool frontend_unified_q2_owner_retire(frontend_unified_q2 *, const qa_unified_presentation_event *, qa_error *);
bool frontend_unified_q2_presentation(frontend_unified_q2 *, const qa_unified_presentation_event *, bool *, qa_error *);
bool frontend_unified_q2_simulation(frontend_unified_q2 *, const qa_unified_simulation_event *, qa_error *);
bool frontend_unified_q2_frame_prepare(frontend_unified_q2 *, const qa_unified_document *, qa_error *);
bool frontend_unified_q2_frame_ready(frontend_unified_q2 *, const qa_unified_document *, qa_error *);
void frontend_unified_q2_frame_commit(frontend_unified_q2 *);
void frontend_unified_q2_frame_abort(frontend_unified_q2 *);
bool frontend_unified_q2_world(frontend_unified_q2 *, const qa_scene_view *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_world_models(frontend_unified_q2 *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_world_particles(frontend_unified_q2 *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_world_input(frontend_unified_q2 *, qa_scene_world_input *, qa_error *);
bool frontend_unified_q2_view_origin(frontend_unified_q2 *, qa_actor_id, qa_vec3, float player_fov, qa_error *);
bool frontend_unified_q2_player_blend(frontend_unified_q2 *, qa_actor_id, bool, const qa_vec4 *,
    bool, const qa_vec4 *, qa_scene_rect, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_lights(frontend_unified_q2 *, const qa_scene_view *, const qa_scene_world_input *, const qa_scene_light **, size_t *, qa_error *);
bool frontend_unified_q2_hud(frontend_unified_q2 *, qa_ui *, qa_scene_rect, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_entity_beam(frontend_unified_q2 *,qa_string_id,const qa_scene_view *,
    qa_vec3,qa_vec3,uint32_t,int32_t,qa_scene_frame *,qa_error *);
bool frontend_unified_q2_model(frontend_unified_q2 *, qa_actor_id, qa_string_id, qa_string_id, qa_scene_model_input *, qa_error *);
bool frontend_unified_q2_model_after(frontend_unified_q2 *, qa_actor_id, qa_string_id, qa_string_id, const qa_scene_model_input *, qa_scene_frame *, qa_error *);
bool frontend_unified_q2_idle(const frontend_unified_q2 *);
bool frontend_unified_q2_destroy(frontend_unified_q2 **, qa_error *);
bool frontend_unified_q2_visit(const frontend_unified_q2 *, const qa_application_content_visitor *, qa_error *);
bool frontend_unified_q2_checkpoint_ready(const frontend_unified_q2 *);
#endif
