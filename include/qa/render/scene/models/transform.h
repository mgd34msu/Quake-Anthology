/* Port of src/render/scene/models/transform.ts. GPL-2.0-or-later. */
#ifndef QA_RENDER_SCENE_MODELS_TRANSFORM_H
#define QA_RENDER_SCENE_MODELS_TRANSFORM_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

/* TS at(): index must be an in-range integer index; aborts with the
 * exact TS RangeError otherwise. Element-size generic form. */
const void *qa_model_at(const void *values, size_t count, size_t element_size,
                        double index, const char *label);

qa_vec3 qa_model_world_direction(qa_model_transform transform, qa_vec3 value);
qa_vec3 qa_model_world_point(qa_model_transform transform, qa_vec3 value);
qa_vec3 qa_q3_model_view_origin(qa_vec3 origin, qa_axis axis, qa_vec3 camera,
                                bool non_normalized_axes);
qa_vec3 qa_model_local_delta(qa_model_transform transform, qa_vec3 value);
qa_model_transform qa_compose_model_transform(qa_model_transform parent,
                                              qa_model_transform child);
qa_bounds qa_model_world_bounds(qa_model_transform transform, qa_bounds bounds);

/* TS ModelTag & {scale}. False is TS null. */
typedef struct qa_model_attachment_tag_result {
  qa_model_tag tag;
  double scale;
} qa_model_attachment_tag_result;
bool qa_model_attachment_tag(const qa_scene_entity *entity, const char *name,
                             qa_model_attachment_tag_result *out);
qa_scene_entity qa_attach_scene_entity(const qa_scene_entity *parent,
                                       const qa_scene_entity *child,
                                       qa_model_attachment_tag_result tag);

#endif
