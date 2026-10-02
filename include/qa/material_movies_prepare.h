#ifndef QA_MATERIAL_MOVIES_PREPARE_H
#define QA_MATERIAL_MOVIES_PREPARE_H
#include "qa/cinematic.h"
typedef struct qa_material_movies_stage qa_material_movies_stage;
bool qa_material_movies_stage_prepare(qa_material_movies *, qa_scene_resource_policy *,
    qa_material_movies_stage **, qa_error *);
qa_material_movies *qa_material_movies_stage_destination(const qa_material_movies_stage *);
bool qa_material_movies_stage_ready(qa_material_movies_stage *, qa_error *);
bool qa_material_movies_stage_ready_is(const qa_material_movies_stage *);
void qa_material_movies_stage_publish(qa_material_movies_stage *);
bool qa_material_movies_stage_finish(qa_material_movies_stage **, qa_error *);
bool qa_material_movies_stage_abort(qa_material_movies_stage **, qa_error *);
#endif
