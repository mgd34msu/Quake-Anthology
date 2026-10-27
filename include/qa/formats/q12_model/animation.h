/*
 * Port of src/formats/q12-model/animation.ts. C accommodations: the TS
 * WeakMap caches are dropped — every call builds fresh owned storage
 * with identical values (object identity is not preserved). The TS
 * generic sampleTimedFrame/mapMd2Geometry become void* element /
 * callback forms. GPL-2.0-or-later.
 */
#ifndef QA_FORMATS_Q12_MODEL_ANIMATION_H
#define QA_FORMATS_Q12_MODEL_ANIMATION_H

#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/render.h"
#include "qa/contracts/scene.h"

/* Borrowed element pointer from the timed frames. */
const void *qa_sample_timed_frame(const qa_timed_frames *frames, double time_seconds, double sync_base);
const qa_q1_alias_frame *qa_q1_sample_alias_frame(const qa_q1_alias_frame_set *frames,
                                                  double time_seconds, double sync_base);

/* Malloc'd interpolated vertices; caller frees. */
qa_model_vertex *qa_interpolate_alias_frames(const qa_model_frame *current, const qa_model_frame *previous,
                                             double back_lerp, qa_vec3 previous_origin_delta,
                                             size_t *out_count);

typedef struct qa_alias_mesh_vertex {
  qa_model_vertex vertex;
  qa_vec2 tex_coord;
} qa_alias_mesh_vertex;

typedef struct qa_alias_geometry {
  qa_alias_mesh_vertex *vertices; /* owned */
  size_t vertex_count;
  double *indices; /* owned */
  size_t index_count;
} qa_alias_geometry;

void qa_build_mdl_geometry(const qa_q1_alias_model *model, const qa_model_vertex *pose,
                           size_t pose_count, qa_alias_geometry *out);
void qa_build_md2_geometry(const qa_q2_alias_model *model, const qa_model_vertex *pose,
                           size_t pose_count, qa_alias_geometry *out);
void qa_alias_geometry_destroy(qa_alias_geometry *geometry);

/* mapMd2Geometry<T>: writes one element of elem_size per corner. */
typedef void (*qa_md2_map_fn)(void *ctx, qa_model_vertex vertex, qa_vec2 tex_coord, size_t corner,
                              void *out_element);
void *qa_map_md2_geometry(const qa_q2_alias_model *model, const qa_model_vertex *pose, size_t pose_count,
                          size_t elem_size, qa_md2_map_fn map, void *ctx, double **out_indices,
                          size_t *out_count);

/* Levels borrow model pixels; free only the wrappers. */
qa_render_image qa_mdl_skin_image(const qa_q1_alias_model *model, double skin, qa_palette palette,
                                  double time_seconds, double sync_base);
qa_render_image qa_spr_frame_image(const qa_q1_sprite_model *model, double frame, qa_palette palette,
                                   double time_seconds, double sync_base);
void qa_q12_render_image_destroy(qa_render_image *image);

#endif
