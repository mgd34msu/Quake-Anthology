#ifndef QA_SCENE_BOXED_SKY_INTERNAL_H
#define QA_SCENE_BOXED_SKY_INTERNAL_H

#include "internal.h"

bool qaw_boxed_sky_collect(qa_scene_boxed_sky *, const qa_scene_mesh *,
                           const qa_material_context *, qa_scene_frame *, qa_error *);
bool qaw_boxed_sky_world_end(qa_scene_boxed_sky *, const qa_scene_world *,
                            qa_scene_frame *, size_t, qa_error *);

#endif
