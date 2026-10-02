#ifndef QA_FRONTEND_COMPONENT_SCENE_H
#define QA_FRONTEND_COMPONENT_SCENE_H
#include "internal.h"
#include "../application/guest_q3_component_scene_factory.h"
bool frontend_component_scene_prepare(void *,const application_q3_component_scene_preparation *,qa_error *);
bool frontend_component_scenes_idle(const qa_frontend *);
#endif
