#ifndef QA_FRONTEND_REMOTE_UNIFIED_COMPONENTS_H
#define QA_FRONTEND_REMOTE_UNIFIED_COMPONENTS_H
#include "remote_unified_media.h"
#include "qa/ui.h"
#include "qa/q3_source_scene_bank.h"

typedef struct frontend_unified_components frontend_unified_components;
typedef struct frontend_unified_component_frame frontend_unified_component_frame;
struct frontend_unified_events;
bool frontend_unified_components_events_bind(frontend_unified_components *,struct frontend_unified_events *,qa_error *);
bool frontend_unified_components_create(qa_frontend *,frontend_remote_unified *,frontend_unified_media *,
    frontend_unified_components **,qa_error *);
bool frontend_unified_components_control(frontend_unified_components *,const qa_unified_document *,qa_error *);
bool frontend_unified_components_frame_prepare(frontend_unified_components *,const qa_unified_document *,
    frontend_unified_component_frame **,bool *ready,qa_error *);
void frontend_unified_components_frame_commit(frontend_unified_component_frame **);
void frontend_unified_components_frame_abort(frontend_unified_component_frame **);
bool frontend_unified_components_frame_ready(const frontend_unified_component_frame *,const qa_unified_document *,qa_error *);
bool frontend_unified_components_prepare_draw(frontend_unified_components *,const qa_scene_view *,uint64_t sequence,qa_error *);
bool frontend_unified_components_lights(frontend_unified_components *,const qa_scene_light **,size_t *,qa_error *);
bool frontend_unified_components_submit(frontend_unified_components *,qa_q3_presentation *,qa_q3_source_scene_bank *,
    const qa_q3_scene_options *,qa_scene_frame *,qa_error *);
bool frontend_unified_components_recipient_content(const frontend_unified_components *,const char **,
    const qa_recipe_provider **,bool *,qa_error *);
bool frontend_unified_components_recipient_current(const frontend_unified_components *,const char *,
    const qa_recipe_provider *);
bool frontend_unified_components_world(frontend_unified_components *,const qa_scene_view *,
    const qa_scene_world_input *,qa_scene_frame *,qa_error *);
bool frontend_unified_components_hud(frontend_unified_components *,qa_ui *,qa_scene_rect,qa_scene_frame *,qa_error *);
bool frontend_unified_components_current(const frontend_unified_components *);
bool frontend_unified_components_idle(const frontend_unified_components *);
bool frontend_unified_components_destroy(frontend_unified_components **,qa_error *);
bool frontend_unified_components_visit(const frontend_unified_components *,const qa_application_content_visitor *,qa_error *);
#endif
