/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QAW_Q3_PATCH_H
#define QAW_Q3_PATCH_H

#include "../internal.h"

enum { QAW_PATCH_LIMIT = 65 };
struct qaw_patch {
    unsigned width, height;
    float width_error[QAW_PATCH_LIMIT], height_error[QAW_PATCH_LIMIT];
    qa_vec3 lod_origin;
    float lod_radius;
    bool stitched, fixed;
};

bool qaw_patch_build(qaw_surface *, const qa_bsp_surface *, float subdivisions, qa_error *);
bool qaw_patch_prepare(qa_scene_world *, qa_error *);
bool qaw_patch_lod(const qaw_surface *, const qa_material_context *, float curve_error,
                   qa_scene_frame *, qa_scene_mesh *, qa_error *);

#endif
