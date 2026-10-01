#ifndef QA_SCENE_WORLD_Q3_INTERNAL_H
#define QA_SCENE_WORLD_Q3_INTERNAL_H
#include "../internal.h"
#include "patch.h"
/* Pure native immutable RGB conversion, shared by production and readonly
 * restored source qualification. */
void qaw_q3_shift_color(const uint8_t input[3], uint32_t shift, uint8_t output[3]);
typedef struct q3_grid_layout {
    qa_vec3 size, origin, inverse;
    size_t bounds[3], count;
} q3_grid_layout;
bool qaw_q3_grid_layout(const qa_scene_world *, q3_grid_layout *, qa_error *);
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
