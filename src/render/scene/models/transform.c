/* Port of src/render/scene/models/transform.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/transform.h"

#include <math.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/formats/q3_model/md3.h"
#include "qa/formats/q3_model/md5.h"
#include "qa/formats/q3_model/scene.h"
#include "qa/foundation/support.h"

static double fr(double value) { return (double)(float)value; }

const void *qa_model_at(const void *values, size_t count, size_t element_size,
                        double index, const char *label) {
  /* TS values[index] is undefined unless index is an in-range
   * integer array index. */
  if (values == NULL || !(index >= 0) || trunc(index) != index ||
      index >= (double)count)
    qa_range_error("Missing %s %g", label, index);
  return (const char *)values + (size_t)index * element_size;
}

qa_vec3 qa_model_world_direction(qa_model_transform transform, qa_vec3 value) {
  qa_vec3 forward = transform.axis.vectors[0], left = transform.axis.vectors[1],
          up = transform.axis.vectors[2];
  double x = value.x * transform.scale.x, y = value.y * transform.scale.y,
         z = value.z * transform.scale.z;
  qa_vec3 out = {
      fr(fr(fr(forward.x * x) + fr(left.x * y)) + fr(up.x * z)),
      fr(fr(fr(forward.y * x) + fr(left.y * y)) + fr(up.y * z)),
      fr(fr(fr(forward.z * x) + fr(left.z * y)) + fr(up.z * z))};
  return out;
}

qa_vec3 qa_model_world_point(qa_model_transform transform, qa_vec3 value) {
  qa_vec3 forward = transform.axis.vectors[0], left = transform.axis.vectors[1],
          up = transform.axis.vectors[2];
  double x = value.x * transform.scale.x, y = value.y * transform.scale.y,
         z = value.z * transform.scale.z;
  qa_vec3 out = {
      fr(transform.origin.x + fr(fr(fr(forward.x * x) + fr(left.x * y)) + fr(up.x * z))),
      fr(transform.origin.y + fr(fr(fr(forward.y * x) + fr(left.y * y)) + fr(up.y * z))),
      fr(transform.origin.z + fr(fr(fr(forward.z * x) + fr(left.z * y)) + fr(up.z * z)))};
  return out;
}

qa_vec3 qa_q3_model_view_origin(qa_vec3 origin, qa_axis axis, qa_vec3 camera,
                                bool non_normalized_axes) {
  qa_vec3 delta = qa_sub3(camera, origin);
  double length = qa_length3(axis.vectors[0]);
  double scale = non_normalized_axes ? (length == 0 ? 0 : fr(1 / length)) : 1;
  return qa_math_vec3(qa_dot3(delta, axis.vectors[0]) * scale,
                      qa_dot3(delta, axis.vectors[1]) * scale,
                      qa_dot3(delta, axis.vectors[2]) * scale);
}

qa_vec3 qa_model_local_delta(qa_model_transform transform, qa_vec3 value) {
  qa_vec3 a = qa_scale3(transform.axis.vectors[0], transform.scale.x);
  qa_vec3 b = qa_scale3(transform.axis.vectors[1], transform.scale.y);
  qa_vec3 c = qa_scale3(transform.axis.vectors[2], transform.scale.z);
  qa_vec3 bc = qa_cross3(b, c), ca = qa_cross3(c, a), ab = qa_cross3(a, b);
  double determinant = qa_dot3(a, bc);
  if (determinant == 0) qa_range_error("Model transform is singular");
  qa_vec3 out = {qa_dot3(value, bc) / determinant,
                 qa_dot3(value, ca) / determinant,
                 qa_dot3(value, ab) / determinant};
  return out;
}

qa_model_transform qa_compose_model_transform(qa_model_transform parent,
                                              qa_model_transform child) {
  qa_model_transform out;
  out.axis.vectors[0] =
      qa_model_world_direction(parent, qa_scale3(child.axis.vectors[0], child.scale.x));
  out.axis.vectors[1] =
      qa_model_world_direction(parent, qa_scale3(child.axis.vectors[1], child.scale.y));
  out.axis.vectors[2] =
      qa_model_world_direction(parent, qa_scale3(child.axis.vectors[2], child.scale.z));
  out.origin = qa_model_world_point(parent, child.origin);
  out.scale = (qa_vec3){1, 1, 1};
  return out;
}

qa_bounds qa_model_world_bounds(qa_model_transform transform, qa_bounds bounds) {
  qa_bounds result = qa_empty_bounds();
  for (int corner = 0; corner < 8; corner++) {
    qa_vec3 point = {(corner & 1) ? bounds.max.x : bounds.min.x,
                     (corner & 2) ? bounds.max.y : bounds.min.y,
                     (corner & 4) ? bounds.max.z : bounds.min.z};
    result = qa_add_point_to_bounds(result, qa_model_world_point(transform, point));
  }
  return result;
}

bool qa_model_attachment_tag(const qa_scene_entity *entity, const char *name,
                             qa_model_attachment_tag_result *out) {
  const qa_decoded_model *model = &entity->model;
  const qa_model_pose *pose = &entity->pose;
  if (model->kind == QA_DECODED_MODEL_Q3_MESH && pose->is_frame) {
    const qa_q3_mesh_model *mesh = model->as.q3_mesh;
    if (mesh == NULL) return false;
    /* TS model.tags[Math.min(frame, frames.length - 1)]?.find(...). */
    const qa_model_tag *first = NULL, *second = NULL;
    double wanted[2] = {pose->previous_frame, pose->frame};
    const qa_model_tag **found[2] = {&first, &second};
    for (int which = 0; which < 2; which++) {
      double row = wanted[which] < (double)mesh->frame_count - 1
                       ? wanted[which]
                       : (double)mesh->frame_count - 1;
      if (row >= 0 && trunc(row) == row && (size_t)row < mesh->tag_row_count) {
        const qa_model_tag *tags = mesh->tag_rows[(size_t)row];
        size_t count = mesh->tag_row_counts[(size_t)row];
        for (size_t i = 0; i < count; i++)
          if (tags[i].name != NULL && strcmp(tags[i].name, name) == 0) {
            *found[which] = &tags[i];
            break;
          }
      }
    }
    if (first == NULL || second == NULL) return false;
    qa_md3_tag start = {(char *)first->name, first->origin, first->axis};
    qa_md3_tag end = {(char *)second->name, second->origin, second->axis};
    qa_md3_tag tag;
    qa_md3_interpolate_tags(&start, &end, name, 1 - pose->back_lerp, &tag);
    out->tag.name = tag.name;
    out->tag.origin = tag.origin;
    out->tag.axis = tag.axes;
    out->scale = 1;
    return true;
  }
  if (model->kind == QA_DECODED_MODEL_MD5) {
    const qa_md5_model *md5 = model->as.md5;
    if (md5 == NULL) return false;
    long index = -1;
    for (size_t i = 0; i < md5->joint_count; i++)
      if (md5->joints[i].name != NULL && strcmp(md5->joints[i].name, name) == 0) {
        index = (long)i;
        break;
      }
    if (index < 0) return false;
    const qa_skeleton_joint_pose *joints;
    size_t joint_count;
    bool owned = false;
    if (!pose->is_frame) {
      joints = pose->joints;
      joint_count = pose->joint_count;
    } else {
      joints = qa_sample_md5_pose(md5, pose->frame, pose->previous_frame,
                                  pose->back_lerp, &owned, &joint_count);
    }
    if (joints == NULL || (size_t)index >= joint_count) return false;
    qa_joint_attachment_pose joint = qa_joint_attachment_tag(name, joints[index]);
    out->tag = joint.tag;
    out->scale = joint.scale;
    return true;
  }
  return false;
}

qa_scene_entity qa_attach_scene_entity(const qa_scene_entity *parent,
                                       const qa_scene_entity *child,
                                       qa_model_attachment_tag_result tag) {
  qa_model_transform tag_local = {tag.tag.origin, tag.tag.axis,
                                  {tag.scale, tag.scale, tag.scale}};
  qa_model_transform tag_transform =
      qa_compose_model_transform(parent->transform, tag_local);
  qa_model_transform transform =
      qa_compose_model_transform(tag_transform, child->transform);
  qa_vec3 delta = qa_model_world_direction(
      tag_transform, qa_sub3(child->previous_origin, child->transform.origin));
  qa_scene_entity out = *child;
  out.transform = transform;
  out.previous_origin = qa_add3(transform.origin, delta);
  out.lighting_origin = parent->lighting_origin;
  return out;
}
