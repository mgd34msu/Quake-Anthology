/* Port of src/render/scene/models/grip.ts. GPL-2.0-or-later. */
#include "qa/render/scene/models/grip.h"

#include <stdlib.h>
#include <string.h>

#include "qa/core/math.h"
#include "qa/foundation/support.h"
#include "qa/render/scene/models/attachment.h"
#include "qa/render/scene/models/prepare.h"
#include "qa/render/scene/models/transform.h"

static bool str_eq(const char *a, const char *b) {
  if (a == NULL || b == NULL) return a == b;
  return strcmp(a, b) == 0;
}

static bool triangle(qa_vec3 a, qa_vec3 b, qa_vec3 c, qa_model_transform *out) {
  qa_vec3 forward = qa_sub3(b, a);
  qa_vec3 normal = qa_cross3(forward, qa_sub3(c, a));
  double length = qa_length3(forward), area = qa_length3(normal);
  if (length == 0 || area == 0) return false;
  qa_vec3 x = qa_scale3(forward, 1 / length);
  qa_vec3 z = qa_scale3(normal, 1 / area);
  out->origin = a;
  out->axis.vectors[0] = x;
  out->axis.vectors[1] = qa_cross3(z, x);
  out->axis.vectors[2] = z;
  out->scale = (qa_vec3){1, 1, 1};
  return true;
}

void qa_model_grip_destroy(qa_model_grip *grip) {
  free(grip->owned_frames);
  free(grip->owned_vertices);
  free(grip->owned_packed);
  memset(grip, 0, sizeof *grip);
}

bool qa_create_model_grip(const qa_scene_entity *reference,
                          qa_model_attachment_definition definition,
                          qa_model_grip *out) {
  memset(out, 0, sizeof *out);
  if (!str_eq(reference->resource.digest, definition.digest))
    qa_range_error("Model attachment digest differs from its source bytes");
  out->reference_id = reference->resource.id;
  out->grip = definition.grip;
  if (definition.is_joint) {
    out->is_joint = true;
    out->name = definition.name;
    return true;
  }
  if (reference->model.kind != QA_DECODED_MODEL_Q2_ALIAS)
    qa_range_error("Mesh attachment requires its declared MD2 source");
  const qa_q2_alias_model *source = reference->model.as.q2_alias;
  size_t frames = source->frame_count;
  out->owned_frames = calloc(frames ? frames : 1, sizeof *out->owned_frames);
  out->owned_vertices = calloc((frames ? frames : 1) * 3, sizeof *out->owned_vertices);
  out->owned_packed = calloc((frames ? frames : 1) * 3, sizeof *out->owned_packed);
  if (out->owned_frames == NULL || out->owned_vertices == NULL ||
      out->owned_packed == NULL) {
    qa_model_grip_destroy(out);
    return false;
  }
  for (size_t fi = 0; fi < frames; fi++) {
    out->owned_frames[fi] = source->frames[fi];
    for (int k = 0; k < 3; k++) {
      out->owned_vertices[fi * 3 + k] = *(
          const qa_model_vertex *)qa_model_at(
          source->frames[fi].frame.vertices, source->frames[fi].frame.vertex_count,
          sizeof(qa_model_vertex), definition.vertices[k], "attachment vertex");
      out->owned_packed[fi * 3 + k] = *(
          const qa_packed_alias_vertex *)qa_model_at(
          source->frames[fi].compressed_vertices,
          source->frames[fi].compressed_vertex_count,
          sizeof(qa_packed_alias_vertex), definition.vertices[k],
          "attachment packed vertex");
    }
    out->owned_frames[fi].frame.vertices = out->owned_vertices + fi * 3;
    out->owned_frames[fi].frame.vertex_count = 3;
    out->owned_frames[fi].compressed_vertices = out->owned_packed + fi * 3;
    out->owned_frames[fi].compressed_vertex_count = 3;
  }
  out->model = *source;
  out->model.frames = out->owned_frames;
  const qa_q2_alias_frame *reference_frame = (const qa_q2_alias_frame *)qa_model_at(
      out->owned_frames, frames, sizeof(qa_q2_alias_frame),
      definition.reference_frame, "attachment reference frame");
  const qa_model_vertex *positions = reference_frame->frame.vertices;
  size_t position_count = reference_frame->frame.vertex_count;
  qa_model_transform initial;
  qa_vec3 a = ((const qa_model_vertex *)qa_model_at(
                   positions, position_count, sizeof(qa_model_vertex), 0,
                   "attachment vertex"))->position;
  qa_vec3 b = ((const qa_model_vertex *)qa_model_at(
                   positions, position_count, sizeof(qa_model_vertex), 1,
                   "attachment vertex"))->position;
  qa_vec3 c = ((const qa_model_vertex *)qa_model_at(
                   positions, position_count, sizeof(qa_model_vertex), 2,
                   "attachment vertex"))->position;
  if (!triangle(a, b, c, &initial))
    qa_range_error("Model attachment reference is degenerate");
  qa_model_transform identity = {{0, 0, 0}, {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}}, {1, 1, 1}};
  qa_model_transform inverse = qa_align_model_attachment(initial, identity);
  out->relative = qa_compose_model_transform(inverse, definition.grip);
  return true;
}

bool qa_model_grip_transform(const qa_model_grip *grip,
                             const qa_scene_entity *entity,
                             qa_model_transform *out) {
  if (grip->is_joint) {
    if (!str_eq(entity->resource.id, grip->reference_id))
      qa_range_error("Model attachment source changed");
    qa_scene_entity posed = *entity;
    if (entity->pose.is_frame) {
      qa_repaired_frames repaired = qa_repair_frames(entity);
      posed.pose.frame = repaired.frame;
      posed.pose.previous_frame = repaired.previous_frame;
      posed.pose.back_lerp = repaired.back_lerp;
    }
    qa_model_attachment_tag_result tag;
    if (!qa_model_attachment_tag(&posed, grip->name, &tag))
      qa_range_error("Source model has no attachment %s", grip->name);
    qa_model_transform tag_transform = {tag.tag.origin, tag.tag.axis,
                                        {tag.scale, tag.scale, tag.scale}};
    *out = qa_compose_model_transform(tag_transform, grip->grip);
    return true;
  }
  if (!str_eq(entity->resource.id, grip->reference_id) || !entity->pose.is_frame)
    qa_range_error("Mesh attachment lost its original source pose");
  qa_repaired_frames repaired = qa_repair_frames(entity);
  size_t count = 0;
  qa_model_vertex *vertices = qa_interpolate_scene_md2(
      &grip->model, entity, repaired.frame, repaired.previous_frame,
      repaired.back_lerp, false, &count);
  if (vertices == NULL) return false;
  qa_vec3 a = ((const qa_model_vertex *)qa_model_at(
                   vertices, count, sizeof(qa_model_vertex), 0,
                   "attachment vertex"))->position;
  qa_vec3 b = ((const qa_model_vertex *)qa_model_at(
                   vertices, count, sizeof(qa_model_vertex), 1,
                   "attachment vertex"))->position;
  qa_vec3 c = ((const qa_model_vertex *)qa_model_at(
                   vertices, count, sizeof(qa_model_vertex), 2,
                   "attachment vertex"))->position;
  free(vertices);
  qa_model_transform current;
  if (!triangle(a, b, c, &current)) return false;
  *out = qa_compose_model_transform(current, grip->relative);
  return true;
}
