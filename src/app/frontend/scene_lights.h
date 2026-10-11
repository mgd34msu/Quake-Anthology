#ifndef QA_FRONTEND_SCENE_LIGHTS_H
#define QA_FRONTEND_SCENE_LIGHTS_H
#include "source_companion.h"

typedef bool (*frontend_scene_light_read)(void *,qa_scene_light *,
    const qa_q3_refdef **,bool *view,bool *present,qa_error *);
bool frontend_scene_lights_merge(qa_arena *,qa_scene_world_input *,size_t upper_bound,
    frontend_scene_light_read,void *,qa_error *);
bool frontend_scene_lights_span_merge(qa_arena *,qa_scene_world_input *,
    const qa_scene_light *,size_t,qa_error *);
bool frontend_scene_lights_companion_merge(qa_arena *,qa_scene_world_input *,
    const frontend_source_companion_selection *,bool skip_view,qa_error *);
#endif
