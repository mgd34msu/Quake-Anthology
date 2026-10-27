/* Port of src/formats/q12-model/animation.ts. GPL-2.0-or-later. */
#include "qa/formats/q12_model/animation.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/formats/q12_model/common.h"
#include "qa/foundation/support.h"

#define FR(value) ((double)(float)(value))

static void *xmalloc(size_t size) {
  void *p = malloc(size == 0 ? 1 : size);
  if (p == NULL) qa_range_error("Out of memory");
  return p;
}

const void *qa_sample_timed_frame(const qa_timed_frames *frames, double time_seconds, double sync_base) {
  if (!isfinite(time_seconds) || !isfinite(sync_base)) qa_range_error("Animation time must be finite");
  if (!frames->is_group) return frames->single_frame;
  const qa_timed_frame_entry *last = &frames->frames[qa_q12_at((double)frames->frame_count - 1,
                                                              frames->frame_count, "group frame")];
  double time = time_seconds + sync_base;
  double target = time - trunc(time / last->interval_seconds) * last->interval_seconds;
  for (size_t i = 0; i < frames->frame_count; i++) {
    if (frames->frames[i].interval_seconds > target) return frames->frames[i].frame;
  }
  return last->frame;
}

const qa_q1_alias_frame *qa_q1_sample_alias_frame(const qa_q1_alias_frame_set *frames,
                                                  double time_seconds, double sync_base) {
  if (!isfinite(time_seconds) || !isfinite(sync_base)) qa_range_error("Animation time must be finite");
  if (!frames->is_group) return frames->single_frame;
  const qa_timed_frame_entry *last = &frames->frames[qa_q12_at((double)frames->frame_count - 1,
                                                              frames->frame_count, "group frame")];
  double time = time_seconds + sync_base;
  double target = time - trunc(time / last->interval_seconds) * last->interval_seconds;
  for (size_t i = 0; i < frames->frame_count; i++) {
    if (frames->frames[i].interval_seconds > target)
      return (const qa_q1_alias_frame *)frames->frames[i].frame;
  }
  return (const qa_q1_alias_frame *)last->frame;
}

qa_model_vertex *qa_interpolate_alias_frames(const qa_model_frame *current, const qa_model_frame *previous,
                                             double back_lerp, qa_vec3 previous_origin_delta,
                                             size_t *out_count) {
  if (!isfinite(back_lerp) || back_lerp < 0 || back_lerp > 1) qa_range_error("backLerp must be in 0..1");
  if (current->vertex_count != previous->vertex_count)
    qa_range_error("Alias poses have different vertex counts");
  double front_lerp = 1 - back_lerp;
  qa_model_vertex *result = xmalloc(current->vertex_count * sizeof *result);
  for (size_t i = 0; i < current->vertex_count; i++) {
    const qa_model_vertex *old = &previous->vertices[qa_q12_at((double)i, previous->vertex_count,
                                                               "previous vertex")];
    result[i].normal = current->vertices[i].normal;
    result[i].position.x = FR(current->vertices[i].position.x * front_lerp +
                              (old->position.x + previous_origin_delta.x) * back_lerp);
    result[i].position.y = FR(current->vertices[i].position.y * front_lerp +
                              (old->position.y + previous_origin_delta.y) * back_lerp);
    result[i].position.z = FR(current->vertices[i].position.z * front_lerp +
                              (old->position.z + previous_origin_delta.z) * back_lerp);
  }
  *out_count = current->vertex_count;
  return result;
}

void qa_build_mdl_geometry(const qa_q1_alias_model *model, const qa_model_vertex *pose,
                           size_t pose_count, qa_alias_geometry *out) {
  size_t corners = model->triangle_count * 3;
  out->vertices = xmalloc(corners * sizeof *out->vertices);
  out->indices = xmalloc(corners * sizeof *out->indices);
  out->vertex_count = 0;
  out->index_count = 0;
  for (size_t t = 0; t < model->triangle_count; t++) {
    const qa_q1_alias_triangle *triangle = &model->triangles[t];
    for (int j = 0; j < 3; j++) {
      double index = triangle->vertices[j];
      const qa_model_vertex *vertex = &pose[qa_q12_at(index, pose_count, "pose vertex")];
      const qa_alias_tex_coord *coordinate =
          &model->texture_coordinates[qa_q12_at(index, model->texture_coordinate_count,
                                                "texture coordinate")];
      double s = coordinate->s + (!triangle->front && coordinate->on_seam ? model->skin_width / 2 : 0);
      out->indices[out->index_count++] = (double)out->vertex_count;
      out->vertices[out->vertex_count].vertex = *vertex;
      out->vertices[out->vertex_count].tex_coord.x = (s + 0.5) / model->skin_width;
      out->vertices[out->vertex_count].tex_coord.y = (coordinate->t + 0.5) / model->skin_height;
      out->vertex_count++;
    }
  }
}

void qa_build_md2_geometry(const qa_q2_alias_model *model, const qa_model_vertex *pose,
                           size_t pose_count, qa_alias_geometry *out) {
  size_t corners = model->triangle_count * 3;
  out->vertices = xmalloc(corners * sizeof *out->vertices);
  out->indices = xmalloc(corners * sizeof *out->indices);
  out->vertex_count = 0;
  out->index_count = 0;
  for (size_t t = 0; t < model->triangle_count; t++) {
    const qa_q2_alias_triangle *triangle = &model->triangles[t];
    for (int corner = 0; corner < 3; corner++) {
      double index = triangle->vertices[corner];
      const qa_model_vertex *vertex = &pose[qa_q12_at(index, pose_count, "pose vertex")];
      double coordinate_index = triangle->tex_coords[corner];
      const qa_vec2 *coordinate =
          &model->texture_coordinates[qa_q12_at(coordinate_index, model->texture_coordinate_count,
                                                "texture coordinate")];
      out->indices[out->index_count++] = (double)out->vertex_count;
      out->vertices[out->vertex_count].vertex = *vertex;
      out->vertices[out->vertex_count].tex_coord.x = (coordinate->x + 0.5) / model->skin_width;
      out->vertices[out->vertex_count].tex_coord.y = (coordinate->y + 0.5) / model->skin_height;
      out->vertex_count++;
    }
  }
}

void qa_alias_geometry_destroy(qa_alias_geometry *geometry) {
  if (geometry == NULL) return;
  free(geometry->vertices);
  free(geometry->indices);
  memset(geometry, 0, sizeof *geometry);
}

void *qa_map_md2_geometry(const qa_q2_alias_model *model, const qa_model_vertex *pose, size_t pose_count,
                          size_t elem_size, qa_md2_map_fn map, void *ctx, double **out_indices,
                          size_t *out_count) {
  qa_alias_geometry geometry;
  qa_build_md2_geometry(model, pose, pose_count, &geometry);
  uint8_t *vertices = xmalloc(geometry.vertex_count * elem_size);
  for (size_t i = 0; i < geometry.vertex_count; i++) {
    map(ctx, geometry.vertices[i].vertex, geometry.vertices[i].tex_coord, i, vertices + i * elem_size);
  }
  *out_indices = geometry.indices;
  *out_count = geometry.vertex_count;
  free(geometry.vertices);
  return vertices;
}

static qa_render_image indexed_image(double width, double height, qa_byte_view pixels, qa_palette palette,
                                     const char *transparency, double transparency_index,
                                     double fullbright_first, double fullbright_last) {
  qa_render_image image;
  memset(&image, 0, sizeof image);
  qa_image_level *level = xmalloc(sizeof *level);
  level->width = width;
  level->height = height;
  level->pixels = pixels;
  qa_fullbright_range *fullbright = xmalloc(sizeof *fullbright);
  fullbright->first = fullbright_first;
  fullbright->last = fullbright_last;
  image.kind = QA_RENDER_IMAGE_INDEXED8;
  image.levels = level;
  image.level_count = 1;
  image.palette = palette;
  image.transparency.kind = transparency;
  image.transparency.index = transparency_index;
  image.fullbright = fullbright;
  image.translation = NULL;
  return image;
}

qa_render_image qa_mdl_skin_image(const qa_q1_alias_model *model, double skin, qa_palette palette,
                                  double time_seconds, double sync_base) {
  const qa_timed_frames *frames = &model->skins[qa_q12_at(skin, model->skin_count, "skin")];
  const qa_byte_view *pixels = qa_sample_timed_frame(frames, time_seconds, sync_base);
  return indexed_image(model->skin_width, model->skin_height, *pixels, palette, "opaque", 0, 224, 255);
}

qa_render_image qa_spr_frame_image(const qa_q1_sprite_model *model, double frame, qa_palette palette,
                                   double time_seconds, double sync_base) {
  const qa_timed_frames *frames = &model->frames[qa_q12_at(frame, model->frame_count, "sprite frame")];
  const qa_sprite_frame *selected = qa_sample_timed_frame(frames, time_seconds, sync_base);
  return indexed_image(selected->width, selected->height, selected->pixels, palette, "index", 255, 224, 254);
}

void qa_q12_render_image_destroy(qa_render_image *image) {
  if (image == NULL) return;
  free((void *)image->levels);
  free((void *)image->fullbright);
  memset(image, 0, sizeof *image);
}
