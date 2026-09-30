#ifndef QA_SCENE_WORLD_Q3_INTERNAL_H
#define QA_SCENE_WORLD_Q3_INTERNAL_H
#include "../internal.h"
#include "patch.h"
typedef struct q3_fog {
    qa_scene_fog fog;
    qa_bounds bounds;
    qa_scene_plane surface;
    float tc_scale;
    bool active, has_surface;
} q3_fog;
typedef struct q3_data {
    qa_scene_image **lightmaps;
    size_t lightmap_count;
    q3_fog *fogs;
    size_t fog_count;
    qa_bsp_grid_point *grid;
    size_t grid_count, grid_bounds[3];
    qa_vec3 grid_origin, grid_inverse;
} q3_data;
#endif
