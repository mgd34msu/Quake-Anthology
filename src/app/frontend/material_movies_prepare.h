#ifndef QA_FRONTEND_MATERIAL_MOVIES_PREPARE_H
#define QA_FRONTEND_MATERIAL_MOVIES_PREPARE_H
#include "material_movies.h"
typedef struct frontend_material_movies_policy frontend_material_movies_policy;
/* The actual bank and material destination already exist. This child installs
 * its prepared callback before replacement skins register new shaders. */
bool frontend_material_movies_policy_prepare(frontend_material_movies *,
    qa_scene_resource_policy *, qa_scene_material_image_policy *,
    qa_q3_cinematic_handles_stage *,
    frontend_material_movies_policy **, qa_error *);
bool frontend_material_movies_policy_current(const frontend_material_movies_policy *,
    const frontend_material_movies *, const qa_scene_resource_policy *,
    const qa_scene_material_image_policy *);
bool frontend_material_movies_policy_ready(frontend_material_movies_policy *, qa_error *);
bool frontend_material_movies_policy_ready_is(const frontend_material_movies_policy *);
void frontend_material_movies_policy_publish(frontend_material_movies_policy *);
/* Dispose order/material destinations first, then this child, then its bank. */
bool frontend_material_movies_policy_finish(frontend_material_movies_policy **, qa_error *);
bool frontend_material_movies_policy_abort(frontend_material_movies_policy **, qa_error *);
#endif
