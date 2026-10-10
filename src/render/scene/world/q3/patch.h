#ifndef QAW_Q3_PATCH_H
#define QAW_Q3_PATCH_H

#include "../internal.h"

enum { QAW_PATCH_LIMIT = 65 };
typedef struct qaw_patch_level {
    float threshold;
    unsigned columns, rows;
    qa_scene_mesh mesh;
    qaw_brush_geometry brush;
} qaw_patch_level;
struct qaw_patch {
    unsigned width, height;
    float width_error[QAW_PATCH_LIMIT], height_error[QAW_PATCH_LIMIT];
    qa_vec3 lod_origin;
    float lod_radius;
    bool stitched, fixed;
    qaw_patch_level *levels;
    size_t level_count;
};

bool qaw_patch_build(qaw_surface *, const qa_bsp_surface *, float subdivisions, qa_error *);
bool qaw_patch_prepare(qa_scene_world *, qa_error *);
void qaw_patch_lod(const qaw_surface *, qa_material_context *, float curve_error, qa_scene_mesh *);
void qaw_patch_destroy(qaw_patch *);

#endif
