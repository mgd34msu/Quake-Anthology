#ifndef QA_FRONTEND_SCENE_LIGHTS_H
#define QA_FRONTEND_SCENE_LIGHTS_H
#include "source_companion.h"
#include "qa/application_q3_components.h"

typedef bool (*frontend_scene_light_read)(void *,qa_scene_light *,
    const qa_q3_refdef **,bool *view,bool *present,qa_error *);
bool frontend_scene_lights_merge(qa_arena *,qa_scene_world_input *,size_t upper_bound,
    frontend_scene_light_read,void *,bool project,qa_error *);
bool frontend_scene_lights_span_merge(qa_arena *,qa_scene_world_input *,
    const qa_scene_light *,size_t,bool project,qa_error *);
bool frontend_scene_lights_companion_merge(qa_frontend *,qa_arena *,qa_scene_world_input *,
    const frontend_source_companion_selection *,bool skip_view,qa_error *);
typedef struct frontend_scene_component_span {
    const qa_frontend *frontend;
    const qa_application_q3_component_draw *draws;
    size_t count,stride;
} frontend_scene_component_span;
bool frontend_scene_components_prepare(qa_arena *,const frontend_scene_component_span *,
    qa_q3_scene_options *,qa_error *);
bool frontend_scene_components_submit(const frontend_scene_component_span *,qa_q3_presentation *,
    const qa_q3_scene_options *,uint32_t first_order,qa_scene_frame *,qa_error *);
#endif
