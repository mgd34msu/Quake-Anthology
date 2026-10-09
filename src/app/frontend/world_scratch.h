#ifndef QA_FRONTEND_WORLD_SCRATCH_H
#define QA_FRONTEND_WORLD_SCRATCH_H
#include "qa/scene.h"

typedef struct frontend_world_scratch {
    qa_scene_world_scratch *view, *child;
} frontend_world_scratch;

bool frontend_world_scratch_create(const qa_scene_world *, frontend_world_scratch *, qa_error *);
void frontend_world_scratch_destroy(frontend_world_scratch *);

#endif
