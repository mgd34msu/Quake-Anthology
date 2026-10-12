#ifndef QA_SCENE_EFFECTS_INTERNAL_H
#define QA_SCENE_EFFECTS_INTERNAL_H

#include "qa/scene.h"
#define QA_EFFECT_PI 3.14159265358979323846

bool qa_effect_mesh(qa_scene_frame *, size_t vertices, size_t indices,
                    qa_scene_mesh *, qa_scene_vertex **, uint32_t **, qa_error *);
void qa_effect_draw(qa_scene_draw *, const qa_scene_view *, const qa_scene_mesh *,
                    const qa_scene_image *, bool additive);
void qa_effect_bounds(qa_scene_mesh *);
qa_vec3 qa_effect_sky_vector(unsigned face, float s, float t, float radius);

#endif
