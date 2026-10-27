/* Port of src/formats/q3-model/scene.ts. GPL-2.0-or-later. */
#include "qa/formats/q3_model/scene.h"

#include <stdlib.h>
#include <string.h>

#include "qa/formats/q3_model/quaternion.h"
#include "qa/foundation/support.h"

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

void qa_to_scene_md3(const qa_md3_model *model, qa_decoded_scene_md3 *out) {
  memset(out, 0, sizeof *out);
  out->source_model = model;
  qa_q3_mesh_model *scene = &out->model;
  scene->name = model->name;
  qa_q3_mesh_frame *frames = xmalloc(model->frame_count * sizeof *frames);
  for (size_t i = 0; i < model->frame_count; i++) {
    frames[i].name = model->frames[i].name;
    frames[i].bounds = model->frames[i].bounds;
    frames[i].local_origin = model->frames[i].origin;
    frames[i].radius = model->frames[i].radius;
  }
  scene->frames = frames;
  scene->frame_count = model->frame_count;
  qa_model_tag **tag_rows = xmalloc(model->tag_row_count * sizeof *tag_rows);
  size_t *tag_counts = xmalloc(model->tag_row_count * sizeof *tag_counts);
  for (size_t i = 0; i < model->tag_row_count; i++) {
    qa_model_tag *row = xmalloc(model->tag_row_counts[i] * sizeof *row);
    for (size_t j = 0; j < model->tag_row_counts[i]; j++) {
      row[j].name = model->tag_rows[i][j].name;
      row[j].origin = model->tag_rows[i][j].origin;
      row[j].axis = model->tag_rows[i][j].axes;
    }
    tag_rows[i] = row;
    tag_counts[i] = model->tag_row_counts[i];
  }
  scene->tag_rows = (const qa_model_tag *const *)tag_rows;
  scene->tag_row_counts = tag_counts;
  scene->tag_row_count = model->tag_row_count;
  qa_q3_mesh_surface *surfaces = xmalloc(model->surface_count * sizeof *surfaces);
  for (size_t i = 0; i < model->surface_count; i++) {
    const qa_md3_surface *surface = &model->surfaces[i];
    surfaces[i].name = surface->name;
    const char **shader_names = xmalloc(surface->shader_count * sizeof *shader_names);
    for (size_t j = 0; j < surface->shader_count; j++) shader_names[j] = surface->shaders[j].name;
    surfaces[i].shaders.values = shader_names;
    surfaces[i].shaders.count = surface->shader_count;
    surfaces[i].texture_coordinates = surface->tex_coords;
    surfaces[i].texture_coordinate_count = surface->tex_coord_count;
    double *indices = xmalloc(surface->triangle_count * 3 * sizeof *indices);
    for (size_t t = 0; t < surface->triangle_count; t++)
      for (int j = 0; j < 3; j++) indices[t * 3 + (size_t)j] = surface->triangles[t].indices[j];
    surfaces[i].indices.values = indices;
    surfaces[i].indices.count = surface->triangle_count * 3;
    surfaces[i].frame_vertices = (const qa_model_vertex *const *)surface->frame_vertices;
    surfaces[i].frame_vertex_counts = surface->frame_vertex_counts;
    surfaces[i].frame_count = surface->frame_count;
  }
  scene->surfaces = surfaces;
  scene->surface_count = model->surface_count;
}

void qa_decoded_scene_md3_destroy(qa_decoded_scene_md3 *model) {
  if (model == NULL) return;
  qa_q3_mesh_model *scene = &model->model;
  free((void *)scene->frames);
  for (size_t i = 0; i < scene->tag_row_count; i++) free((void *)scene->tag_rows[i]);
  free((void *)scene->tag_rows);
  free((void *)scene->tag_row_counts);
  for (size_t i = 0; i < scene->surface_count; i++) {
    free((void *)scene->surfaces[i].shaders.values);
    free((void *)scene->surfaces[i].indices.values);
  }
  free((void *)scene->surfaces);
  memset(model, 0, sizeof *model);
}

qa_joint_attachment_pose qa_joint_attachment_tag(const char *name, qa_skeleton_joint_pose pose) {
  qa_joint_attachment_pose result;
  qa_vec3 x_axis = {1, 0, 0}, y_axis = {0, 1, 0}, z_axis = {0, 0, 1};
  result.tag.name = name;
  result.tag.origin = pose.position;
  result.tag.axis.vectors[0] = qa_rotate_quaternion_axis(pose.orientation, x_axis);
  result.tag.axis.vectors[1] = qa_rotate_quaternion_axis(pose.orientation, y_axis);
  result.tag.axis.vectors[2] = qa_rotate_quaternion_axis(pose.orientation, z_axis);
  result.scale = pose.scale;
  return result;
}
