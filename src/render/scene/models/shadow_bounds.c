/* Port of src/render/scene/models/shadow-bounds.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/shadow_bounds.h"

#include <math.h>
#include <stdlib.h>

#include "qa/formats/q3_model/quaternion.h"
#include "qa/foundation/js.h"

static const double ROUNDOFF = 0x1p-22, UNDERFLOW = 1e-30, MAXIMUM = 1e30;

static double vlength(qa_vec3 v) {
  return qa_js_hypot3(v.x, v.y, v.z); /* TS Math.hypot(v.x, v.y, v.z) */
}
static bool bounded(double value) {
  return isfinite(value) && value >= 0 && value <= MAXIMUM;
}
static double dot(qa_vec3 a, qa_vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

typedef struct weight_envelope {
  double *joint_radii; /* by joint index, -1 when absent */
  size_t joint_capacity;
  double bias_sum;
  double count;
} weight_envelope;

static bool weights_of(const qa_md5_mesh *mesh, weight_envelope *out) {
  out->joint_radii = NULL;
  out->joint_capacity = 0;
  double bias_sum = 0, count = 0;
  for (size_t vi = 0; vi < mesh->vertex_count; vi++) {
    qa_index_range w = mesh->vertices[vi].weights;
    if (trunc(w.count) != w.count || w.count < 0 || w.count > 4096 ||
        trunc(w.first) != w.first || w.first < 0)
      goto fail;
    if (w.count > count) count = w.count;
    double sum = 0;
    for (double i = 0; i < w.count; i++) {
      double wi = w.first + i;
      if (!(wi >= 0) || trunc(wi) != wi || wi >= (double)mesh->weight_count)
        goto fail;
      const qa_md5_mesh_weight *weight = &mesh->weights[(size_t)wi];
      if (trunc(weight->joint) != weight->joint || weight->joint < 0) goto fail;
      double radius = vlength(weight->position);
      sum += fabs(weight->bias);
      if (!bounded(radius) || !bounded(sum)) goto fail;
      size_t joint = (size_t)weight->joint;
      if (joint >= out->joint_capacity) {
        size_t next = joint + 1;
        double *grown = realloc(out->joint_radii, next * sizeof *grown);
        if (grown == NULL) goto fail;
        for (size_t k = out->joint_capacity; k < next; k++) grown[k] = -1;
        out->joint_radii = grown;
        out->joint_capacity = next;
      }
      if (radius > out->joint_radii[joint]) out->joint_radii[joint] = radius;
    }
    if (sum > bias_sum) bias_sum = sum;
  }
  out->bias_sum = bias_sum * (1 + ROUNDOFF);
  out->count = count;
  return true;
fail:
  free(out->joint_radii);
  out->joint_radii = NULL;
  return false;
}

static double operator_norm(qa_vec3 a, qa_vec3 b, qa_vec3 c) {
  double ab = fabs(dot(a, b)), ac = fabs(dot(a, c)), bc = fabs(dot(b, c));
  double m = qa_js_max(dot(a, a) + ab + ac, qa_js_max(dot(b, b) + ab + bc, dot(c, c) + ac + bc));
  return sqrt(m) * (1 + ROUNDOFF);
}

bool qa_md5_shadow_envelope(const qa_md5_mesh *meshes, size_t mesh_count,
                            const qa_skeleton_joint_pose *joints,
                            size_t joint_count, qa_model_transform transform,
                            qa_render_scene_shadow_sphere *out) {
  static const qa_vec3 UNIT[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  double radius = 0;
  for (size_t mi = 0; mi < mesh_count; mi++) {
    weight_envelope metadata;
    if (!weights_of(&meshes[mi], &metadata)) return false;
    double point_radius = 0;
    bool ok = true;
    for (size_t ji = 0; ok && ji < metadata.joint_capacity; ji++) {
      double weight_radius = metadata.joint_radii[ji];
      if (weight_radius < 0) continue;
      if (ji >= joint_count || !bounded(fabs(joints[ji].scale))) { ok = false; break; }
      const qa_skeleton_joint_pose *joint = &joints[ji];
      double norm = operator_norm(
          qa_rotate_quaternion_axis(joint->orientation, UNIT[0]),
          qa_rotate_quaternion_axis(joint->orientation, UNIT[1]),
          qa_rotate_quaternion_axis(joint->orientation, UNIT[2]));
      double position = vlength(joint->position);
      double rotated = norm * weight_radius * (1 + ROUNDOFF) + UNDERFLOW;
      double point =
          (position + fabs(joint->scale) * rotated) * (1 + ROUNDOFF) + UNDERFLOW;
      if (!bounded(norm) || !bounded(position) || !bounded(rotated) ||
          !bounded(point)) { ok = false; break; }
      if (point > point_radius) point_radius = point;
    }
    double accumulated = (metadata.bias_sum * point_radius +
                          metadata.count * UNDERFLOW) *
                         qa_js_pow(1 + ROUNDOFF, metadata.count);
    free(metadata.joint_radii);
    if (!ok || !bounded(accumulated)) return false;
    if (accumulated > radius) radius = accumulated;
  }
  double scales[3] = {fabs(transform.scale.x), fabs(transform.scale.y),
                      fabs(transform.scale.z)};
  for (int i = 0; i < 3; i++)
    if (!bounded(scales[i]) || !bounded(radius * scales[i]) ||
        !bounded(vlength(transform.axis.vectors[i])))
      return false;
  qa_vec3 columns[3];
  for (int i = 0; i < 3; i++) {
    qa_vec3 axis = transform.axis.vectors[i];
    double scale = i == 0 ? transform.scale.x : i == 1 ? transform.scale.y
                                                        : transform.scale.z;
    columns[i] = (qa_vec3){axis.x * scale, axis.y * scale, axis.z * scale};
  }
  double norm = operator_norm(columns[0], columns[1], columns[2]);
  double frobenius = qa_js_hypot3(vlength(columns[0]), vlength(columns[1]), vlength(columns[2]));
  double origin = vlength(transform.origin);
  double transformed = norm * radius, products = frobenius * radius;
  if (!bounded(norm) || !bounded(frobenius) || !bounded(origin) ||
      !bounded(transformed) || !bounded(products))
    return false;
  double world_radius =
      transformed + 32 * ROUNDOFF * (products + origin) + UNDERFLOW;
  if (!bounded(world_radius)) return false;
  out->origin = transform.origin;
  out->radius = qa_js_max(64, world_radius);
  return true;
}
