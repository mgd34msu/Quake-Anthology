#ifndef QA_FRONTEND_REMOTE_UNIFIED_COMPONENTS_H
#define QA_FRONTEND_REMOTE_UNIFIED_COMPONENTS_H
#include "remote_unified_media.h"
#include "qa/ui.h"
#include "qa/q3_source_scene_bank.h"

typedef struct frontend_unified_components frontend_unified_components;
typedef struct frontend_unified_component_frame frontend_unified_component_frame;
typedef struct application_q3_scene application_q3_scene;
typedef struct frontend_unified_component_scene_association {
    qa_actor_owner owner,service_owner;
    uint64_t generation,frontend_identity;
    uint32_t physical_seat;
    int32_t time_ms;
    qa_actor_id viewer;
    qa_executable_recipe *recipe;
    const qa_recipe_provider *provider;
    void *frontend_owner;
    application_q3_scene *scene;
    qa_q3_presentation_assets *assets;
} frontend_unified_component_scene_association;
bool frontend_unified_components_scene_association_read(const qa_frontend *,const void *actual_context,
    uint64_t frontend_identity,frontend_unified_component_scene_association *);
struct frontend_unified_events;
bool frontend_unified_components_events_bind(frontend_unified_components *,struct frontend_unified_events *,qa_error *);
bool frontend_unified_components_create(qa_frontend *,frontend_remote_unified *,frontend_unified_media *,
    frontend_unified_components **,qa_error *);
bool frontend_unified_components_control(frontend_unified_components *,const qa_unified_document *,qa_error *);
bool frontend_unified_components_player_event(frontend_unified_components *,const qa_unified_document *,qa_json_id,
    bool *handled,qa_error *);
bool frontend_unified_components_player_event_validate(frontend_unified_components *,const qa_unified_document *,qa_json_id,
    bool *handled,qa_error *);
bool frontend_unified_components_checkpoint_ready(const frontend_unified_components *);
bool frontend_unified_components_frame_prepare(frontend_unified_components *,const qa_unified_document *,
    frontend_unified_component_frame **,bool *ready,qa_error *);
void frontend_unified_components_frame_commit(frontend_unified_component_frame **);
void frontend_unified_components_frame_abort(frontend_unified_component_frame **);
bool frontend_unified_components_frame_ready(const frontend_unified_component_frame *,const qa_unified_document *,qa_error *);
bool frontend_unified_components_prepare_draw(frontend_unified_components *,const qa_scene_view *,uint64_t sequence,
    const frontend_unified_recipient_clock *,qa_error *);
bool frontend_unified_components_lights_bank(frontend_unified_components *,qa_q3_source_scene_bank *,const qa_scene_light **,size_t *,qa_error *);
bool frontend_unified_components_submit(frontend_unified_components *,qa_q3_presentation *,qa_q3_source_scene_bank *,
    const qa_q3_scene_options *,qa_scene_frame *,qa_error *);
bool frontend_unified_components_prepare_submission(frontend_unified_components *,qa_q3_source_scene_bank *,
    qa_scene_frame *,qa_error *);
bool frontend_unified_components_submission_restore(frontend_unified_components *,qa_q3_source_scene_bank *,qa_error *);
void frontend_unified_components_submission_release(frontend_unified_components *,const qa_q3_source_scene_bank *);
bool frontend_unified_components_submit_reflected(frontend_unified_components *,qa_q3_presentation *,qa_q3_source_scene_bank *,
    const qa_q3_scene_options *,qa_scene_frame *,qa_error *);
bool frontend_unified_components_recipient_content(const frontend_unified_components *,const char **,
    const qa_recipe_provider **,bool *,qa_error *);
bool frontend_unified_components_recipient_current(const frontend_unified_components *,const char *,
    const qa_recipe_provider *);
bool frontend_unified_components_assets_encode(const frontend_unified_components *,const qa_q3_presentation_assets *,
    uint64_t *frontend_identity,qa_error *);
bool frontend_unified_components_assets_decode(const frontend_unified_components *,uint64_t frontend_identity,
    qa_q3_presentation_assets **,qa_error *);
bool frontend_unified_components_world(frontend_unified_components *,const qa_scene_view *,
    const qa_scene_world_input *,qa_scene_frame *,qa_error *);
bool frontend_unified_components_hud(frontend_unified_components *,qa_ui *,qa_scene_rect,qa_scene_frame *,qa_error *);
bool frontend_unified_components_pictures(frontend_unified_components *,qa_q3_presentation *,qa_scene_frame *,qa_error *);
bool frontend_unified_components_current(const frontend_unified_components *);
bool frontend_unified_components_retained_current(const frontend_unified_components *);
bool frontend_unified_components_idle(const frontend_unified_components *);
bool frontend_unified_components_destroy(frontend_unified_components **,qa_error *);
bool frontend_unified_components_visit(const frontend_unified_components *,const qa_application_content_visitor *,qa_error *);
#endif
