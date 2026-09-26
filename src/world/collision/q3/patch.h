/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef QA_Q3_PATCH_H
#define QA_Q3_PATCH_H

#include "../internal.h"

typedef struct qa_q3_patch qa_q3_patch;
typedef struct qa_q3_shape {
    qa_shape_kind kind;
    qa_vec3 mins, extents;
    float radius;
    qa_vec3 offset;
} qa_q3_shape;

/* Generated collision owns all data. Input points are row-major and borrowed
 * only for generation. Generation failures leave *out unchanged. */
bool qa_q3_patch_create(uint32_t width, uint32_t height, const qa_vec3 *points,
                        qa_q3_patch **out, qa_error *error);
void qa_q3_patch_destroy(qa_q3_patch *patch);
/* A hit changes fraction and the plane's normal/distance only: source patch
 * tracing retains type/signbits from a prior brush impact. */
bool qa_q3_patch_trace(const qa_q3_patch *, qa_vec3 start, qa_vec3 end,
                       const qa_q3_shape *, float *fraction,
                       qa_collision_plane *plane);
bool qa_q3_patch_position(const qa_q3_patch *, qa_vec3 start,
                          const qa_q3_shape *);

#endif
